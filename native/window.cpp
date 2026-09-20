#include "window.h"
#include "editor.h"
#include "theme.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <QFutureWatcher>

namespace {
QLabel *label(const QString &text, const char *name = nullptr) {
    auto *widget = new QLabel(text); widget->setTextFormat(Qt::PlainText);
    if (name) widget->setObjectName(QString::fromLatin1(name));
    return widget;
}
QPushButton *button(const QString &text, const char *name = nullptr) {
    auto *widget = new QPushButton(text); widget->setCursor(Qt::PointingHandCursor);
    if (name) widget->setObjectName(QString::fromLatin1(name));
    return widget;
}
}
Window::Window(QString configPath, QWidget *parent) : QMainWindow(parent), store_(std::move(configPath)), runner_(this), batch_(runner_, this) {
    setWindowTitle(QStringLiteral("Lighter · 项目启动器")); resize(1240, 820); setMinimumSize(920, 660);
    QSettings settings(store_.path() + ".ui.ini", QSettings::IniFormat);
    dark_ = settings.value("dark", false).toBool(); applyTheme(*qApp, dark_);
    restoreGeometry(settings.value("geometry").toByteArray());
    auto *root = new QWidget; auto *layout = new QHBoxLayout(root); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0); setCentralWidget(root);
    auto *sidebar = new QFrame; sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(156);
    auto *side = new QVBoxLayout(sidebar); side->setContentsMargins(12, 16, 12, 12); side->setSpacing(6);
    side->addWidget(label("Lighter", "brand")); side->addSpacing(10);
    auto *group = new QButtonGroup(this); group->setExclusive(true);
    pages_ = new QStackedWidget;
    const QStringList names = {QStringLiteral("项目"), QStringLiteral("端口"), QStringLiteral("配置")};
    for (int i = 0; i < names.size(); ++i) {
        auto *nav = button(names[i], "nav"); nav->setCheckable(true); group->addButton(nav, i); side->addWidget(nav);
        connect(nav, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
        if (!i) nav->setChecked(true);
    }
    connect(pages_, &QStackedWidget::currentChanged, this, [group](int index) { if (auto *nav = group->button(index)) nav->setChecked(true); });
    side->addStretch();
    auto *theme = button(QStringLiteral("切换明暗主题")); side->addWidget(theme);
    connect(theme, &QPushButton::clicked, this, [this] {
        dark_ = !dark_; applyTheme(*qApp, dark_);
        QSettings settings(store_.path() + ".ui.ini", QSettings::IniFormat); settings.setValue("dark", dark_);
    });
    auto *version = label(QStringLiteral("v%1").arg(LIGHTER_VERSION), "muted");
    version->setToolTip(QStringLiteral("Qt 6 · %1").arg(LIGHTER_GIT_COMMIT)); side->addWidget(version);
    layout->addWidget(sidebar); layout->addWidget(pages_, 1);
    pages_->addWidget(projectPage()); pages_->addWidget(portPage()); pages_->addWidget(settingsPage());
    total_ = label({}, "muted"); active_ = label({}, "muted"); idle_ = label({}, "muted");
    statusBar()->addPermanentWidget(total_); statusBar()->addPermanentWidget(active_); statusBar()->addPermanentWidget(idle_);
    auto *shortcut = new QShortcut(QKeySequence::New, this); connect(shortcut, &QShortcut::activated, this, [this] { editProject(true); });
    shortcut = new QShortcut(QKeySequence::Find, this); connect(shortcut, &QShortcut::activated, this, [this] { pages_->setCurrentIndex(0); search_->setFocus(); search_->selectAll(); });
    shortcut = new QShortcut(QKeySequence::Refresh, this); connect(shortcut, &QShortcut::activated, this, &Window::load);
    connect(&batch_, &BatchLauncher::changed, this, [this] {
        updateStats(); updateDetail();
        statusBar()->showMessage(batch_.busy() ? (batch_.stopping() ? QStringLiteral("取消启动…") : QStringLiteral("检查 / 释放端口…")) : QString());
        if (closing_ && !batch_.busy() && runner_.count() == 0) QTimer::singleShot(0, this, &QWidget::close);
    });
    connect(&batch_, &BatchLauncher::error, this, &Window::showError);
    connect(&batch_, &BatchLauncher::confirmationNeeded, this, [this](const QString &details) {
        batch_.confirmPorts(confirm(QStringLiteral("结束以下端口占用进程并启动所有项目？\n\n%1").arg(details)));
    });
    connect(&batch_, &BatchLauncher::projectStarting, this, [this](const QString &id) {
        if (logs_.contains(id)) logs_[id]->clear();
        renderers_[id].reset();
    });
    connect(&runner_, &Runner::output, this, &Window::appendOutput);
    connect(&runner_, &Runner::error, this, &Window::showError);
    connect(&runner_, &Runner::stateChanged, this, [this] {
        refreshList();
        if (closing_ && !batch_.busy() && runner_.count() == 0) QTimer::singleShot(0, this, &QWidget::close);
    });
    // Initial errors are shown only after the main window has entered the event loop.
    QTimer::singleShot(0, this, &Window::load);
}
QWidget *Window::projectPage() {
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(16, 16, 16, 12); layout->setSpacing(12);
    auto *head = new QHBoxLayout;
    head->addWidget(label(QStringLiteral("项目"), "pageTitle")); head->addStretch();
    startupPortsButton_ = button(QStringLiteral("启动端口")); startupPortsButton_->setObjectName("startupPortsButton");
    head->addWidget(startupPortsButton_); connect(startupPortsButton_, &QPushButton::clicked, this, &Window::editStartupPorts);
    batchButton_ = button(QStringLiteral("一键启动"), "primary"); batchButton_->setObjectName("batchButton"); batchButton_->setProperty("primary", true); head->addWidget(batchButton_);
    connect(batchButton_, &QPushButton::clicked, this, [this] {
        if (batch_.busy() || runner_.count()) batch_.stop();
        else if (loaded_) batch_.start(config_);
    });
    new_ = button(QStringLiteral("新建项目")); new_->setToolTip("Ctrl+N"); head->addWidget(new_); layout->addLayout(head);
    connect(new_, &QPushButton::clicked, this, [this] { editProject(true); });
    auto *toolbar = new QHBoxLayout;
    search_ = new QLineEdit; search_->setObjectName("projectSearch"); search_->setPlaceholderText(QStringLiteral("搜索")); search_->setToolTip(QStringLiteral("搜索项目、命令或目录（Ctrl+F）")); toolbar->addWidget(search_, 1);
    filter_ = new QComboBox; filter_->addItems({QStringLiteral("全部状态"), QStringLiteral("运行中"), QStringLiteral("已停止")}); toolbar->addWidget(filter_);
    auto *refresh = button(QStringLiteral("刷新")); refresh->setToolTip("F5"); toolbar->addWidget(refresh); layout->addLayout(toolbar);
    connect(search_, &QLineEdit::textChanged, this, [this] { refreshList(); }); connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refreshList(); }); connect(refresh, &QPushButton::clicked, this, &Window::load);
    auto *splitter = new QSplitter; splitter->setChildrenCollapsible(false); projects_ = new QListWidget; projects_->setObjectName("projectList"); projects_->setMinimumWidth(205); splitter->addWidget(projects_);
    detailStack_ = new QStackedWidget;
    auto *empty = new QFrame; empty->setObjectName("panel"); auto *emptyLayout = new QVBoxLayout(empty); emptyLayout->addStretch();
    auto *emptyTitle = label(QStringLiteral("暂无项目"), "emptyState"); emptyTitle->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyTitle);
    emptyLayout->addStretch(); detailStack_->addWidget(empty);
    auto *detail = new QWidget; auto *content = new QVBoxLayout(detail); content->setContentsMargins(8, 0, 0, 0); content->setSpacing(10);
    auto *top = new QHBoxLayout; title_ = label({}, "sectionTitle"); title_->setWordWrap(true); top->addWidget(title_, 1); state_ = label({}, "badge"); top->addWidget(state_); content->addLayout(top);
    directory_ = label({}, "muted"); directory_->setWordWrap(true); directory_->setTextInteractionFlags(Qt::TextSelectableByMouse); content->addWidget(directory_);
    commands_ = label({}); commands_->setWordWrap(true); commands_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *commandScroll = new QScrollArea; commandScroll->setObjectName("commandScroll"); commandScroll->setWidgetResizable(true); commandScroll->setFrameShape(QFrame::NoFrame); commandScroll->setMaximumHeight(100); commandScroll->setWidget(commands_); content->addWidget(commandScroll);
    auto *actions = new QHBoxLayout; start_ = button(QStringLiteral("▶ 启动"), "primary"); stop_ = button(QStringLiteral("■ 停止"), "danger"); edit_ = button(QStringLiteral("编辑")); remove_ = button(QStringLiteral("删除"));
    actions->addWidget(start_); actions->addWidget(stop_); actions->addStretch(); actions->addWidget(edit_); actions->addWidget(remove_); content->addLayout(actions);
    connect(start_, &QPushButton::clicked, this, &Window::startProject); connect(stop_, &QPushButton::clicked, this, [this] { runner_.stop(selectedId()); });
    connect(edit_, &QPushButton::clicked, this, [this] { editProject(false); }); connect(remove_, &QPushButton::clicked, this, &Window::removeProject);
    auto *logbar = new QHBoxLayout; logbar->addWidget(label(QStringLiteral("运行日志"))); logbar->addStretch(); follow_ = new QCheckBox(QStringLiteral("自动滚动")); follow_->setChecked(true); logbar->addWidget(follow_);
    auto *clear = button(QStringLiteral("清空")); auto *exportLog = button(QStringLiteral("保存日志")); logbar->addWidget(clear); logbar->addWidget(exportLog); content->addLayout(logbar);
    terminal_ = new QTextEdit; terminal_->setObjectName("terminal"); terminal_->setReadOnly(true); terminal_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    emptyLog_ = new QTextDocument(this); terminal_->setDocument(emptyLog_); content->addWidget(terminal_, 1);
    connect(clear, &QPushButton::clicked, this, [this] { const auto id = selectedId(); if (logs_.contains(id)) logs_[id]->clear(); renderers_[id].reset(); });
    connect(exportLog, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存日志"), "project-output.txt", "Text (*.txt)");
        if (path.isEmpty()) return;
        try { ConfigStore::write(path, terminal_->toPlainText().toUtf8()); statusBar()->showMessage(QStringLiteral("日志已保存"), 4000); } catch (const std::exception &e) { showError(QString::fromUtf8(e.what())); }
    });
    detailStack_->addWidget(detail); splitter->addWidget(detailStack_); splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 3); splitter->setSizes({250, 650}); layout->addWidget(splitter, 1);
    connect(projects_, &QListWidget::currentItemChanged, this, [this] { updateDetail(); });
    connect(projects_, &QListWidget::itemDoubleClicked, this, [this] { startProject(); });
    return page;
}
QWidget *Window::portPage() {
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(16, 16, 16, 12); layout->setSpacing(12);
    layout->addWidget(label(QStringLiteral("端口"), "pageTitle"));
    auto *panel = new QFrame; panel->setObjectName("panel"); auto *box = new QVBoxLayout(panel); box->setContentsMargins(16, 16, 16, 16); box->setSpacing(12);
    auto *row = new QHBoxLayout;
    port_ = new QSpinBox; port_->setRange(1, 65535); port_->setValue(8080); port_->setPrefix("TCP  "); row->addWidget(port_, 1);
    portQuery_ = button(QStringLiteral("查询端口"), "primary"); portKill_ = button(QStringLiteral("结束占用进程"), "danger"); portKill_->setEnabled(false); row->addWidget(portQuery_); row->addWidget(portKill_); box->addLayout(row);
    portResult_ = label(QStringLiteral("未查询"), "muted"); portResult_->setWordWrap(true); portResult_->setTextInteractionFlags(Qt::TextSelectableByMouse); box->addWidget(portResult_); layout->addWidget(panel); layout->addStretch();
    connect(portQuery_, &QPushButton::clicked, this, &Window::queryPort); connect(portKill_, &QPushButton::clicked, this, &Window::killPort);
    connect(port_, &QSpinBox::valueChanged, this, [this] { portKill_->setEnabled(false); });
    auto *enter = new QShortcut(QKeySequence(Qt::Key_Return), port_); enter->setContext(Qt::WidgetWithChildrenShortcut); connect(enter, &QShortcut::activated, this, &Window::queryPort);
    return page;
}
QWidget *Window::settingsPage() {
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(16, 16, 16, 12); layout->setSpacing(12);
    layout->addWidget(label(QStringLiteral("配置"), "pageTitle"));
    auto *panel = new QFrame; panel->setObjectName("panel"); auto *box = new QVBoxLayout(panel); box->setContentsMargins(16, 16, 16, 16); box->setSpacing(12);
    box->addWidget(label(QStringLiteral("配置文件")));
    auto *path = label(store_.path(), "muted"); path->setWordWrap(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse); box->addWidget(path);
    path->setToolTip(QStringLiteral("首次保存备份：%1.pre-qt.bak").arg(store_.path()));
    auto *row = new QHBoxLayout; auto *import = button(QStringLiteral("导入配置"), "primary"), *exportButton = button(QStringLiteral("导出配置")), *json = button(QStringLiteral("编辑 JSON"));
    row->addWidget(import); row->addWidget(exportButton); row->addWidget(json); row->addStretch(); box->addLayout(row); layout->addWidget(panel);
    connect(import, &QPushButton::clicked, this, &Window::importConfig); connect(exportButton, &QPushButton::clicked, this, &Window::exportConfig); connect(json, &QPushButton::clicked, this, &Window::editJson);
    auto *github = button("GitHub"); layout->addWidget(github, 0, Qt::AlignLeft);
    connect(github, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://github.com/dreamreflex/project-lighter-tauri")); });
    layout->addStretch(); return page;
}
QString Window::selectedId() const { return projects_->currentItem() ? projects_->currentItem()->data(Qt::UserRole).toString() : QString(); }
const Project *Window::selected() const { const auto id = selectedId(); for (const auto &p : config_.projects) if (p.id == id) return &p; return nullptr; }
void Window::updateStats() {
    total_->setText(QStringLiteral("项目 %1   ").arg(config_.projects.size())); active_->setText(QStringLiteral("运行 %1   ").arg(runner_.count()));
    int stopped = 0; for (const auto &p : config_.projects) if (!runner_.running(p.id)) ++stopped;
    idle_->setText(QStringLiteral("停止 %1   ").arg(stopped)); new_->setEnabled(loaded_ && !batch_.busy());
    batchButton_->setText(batch_.busy() || runner_.count() ? QStringLiteral("一键关闭") : QStringLiteral("一键启动"));
    batchButton_->setEnabled(!batch_.stopping() && (batch_.busy() || runner_.count() || (loaded_ && !config_.projects.isEmpty())));
    startupPortsButton_->setEnabled(loaded_ && !batch_.busy());
    startupPortsButton_->setText(QStringLiteral("启动端口 (%1)").arg(config_.startupPorts.size()));
}
void Window::refreshList(const QString &select) {
    const auto old = select.isEmpty() ? selectedId() : select;
    QSignalBlocker blocker(projects_); projects_->clear();
    for (const auto &p : config_.projects) {
        QString haystack = p.name + " " + p.workingDir + " " + p.script;
        for (const auto &c : p.commands) haystack += " " + c.name + " " + c.command;
        if (!haystack.contains(search_->text(), Qt::CaseInsensitive)) continue;
        const bool running = runner_.running(p.id);
        if ((filter_->currentIndex() == 1 && !running) || (filter_->currentIndex() == 2 && running)) continue;
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2\n     %3 · %4").arg(running ? "●" : "○", p.name, running ? QStringLiteral("运行中") : QStringLiteral("已停止"), p.type == "script" ? QStringLiteral("脚本") : QStringLiteral("%1 个步骤").arg(p.commands.size())), projects_);
        item->setData(Qt::UserRole, p.id); item->setToolTip(p.workingDir);
        if (p.id == old) projects_->setCurrentItem(item);
    }
    if (!projects_->currentItem() && projects_->count()) projects_->setCurrentRow(0);
    updateStats(); updateDetail();
    QSet<QString> retained;
    for (const auto &project : config_.projects) retained.insert(project.id);
    for (const auto &id : logs_.keys()) {
        if (!retained.contains(id) && !runner_.running(id)) {
            auto *document = logs_.take(id);
            if (terminal_->document() == document) terminal_->setDocument(emptyLog_);
            document->deleteLater(); renderers_.remove(id);
        }
    }
}
void Window::updateDetail() {
    const auto *p = selected(); detailStack_->setCurrentIndex(p ? 1 : 0);
    if (!p) {
        findChild<QLabel *>("emptyState")->setText(config_.projects.isEmpty() ? QStringLiteral("暂无项目") : QStringLiteral("无匹配项目"));
        terminal_->setDocument(emptyLog_); return;
    }
    title_->setText(p->name); directory_->setText(p->workingDir.isEmpty() ? QDir::currentPath() : p->workingDir);
    QStringList commands; for (int i = 0; i < p->commands.size(); ++i) commands << QStringLiteral("%1  %2  ›  %3").arg(i + 1).arg(p->commands[i].name, p->commands[i].command);
    commands_->setText(p->type == "script" ? p->script : commands.join('\n'));
    findChild<QScrollArea *>("commandScroll")->setFixedHeight(qBound(42, (p->type == "script" ? int(p->script.count('\n')) + 1 : int(commands.size())) * 30 + 12, 100));
    const bool running = runner_.running(p->id);
    state_->setText(running ? QStringLiteral("● 运行中") : QStringLiteral("○ 已停止")); start_->setEnabled(!running && !batch_.busy()); stop_->setEnabled(running);
    edit_->setEnabled(!running && !batch_.busy()); remove_->setEnabled(!batch_.busy()); edit_->setToolTip(running ? QStringLiteral("停止项目后可编辑") : QString());
    auto *document = logs_.value(p->id, emptyLog_); if (terminal_->document() != document) terminal_->setDocument(document);
}
void Window::load() {
    if (batch_.busy()) return;
    try {
        const auto config = store_.load();
        if (runner_.count() && config.json() != config_.json()) {
            if (!confirm(QStringLiteral("配置已变化。重新加载会停止当前运行的项目，是否继续？"))) return;
            runner_.stopAll();
        }
        config_ = config; loaded_ = true; refreshList(); statusBar()->showMessage(QStringLiteral("配置已加载"), 3000);
    } catch (const std::exception &e) { showError(QStringLiteral("加载配置失败：%1\n可在配置管理中修复 JSON 或导入配置。原文件未修改。").arg(QString::fromUtf8(e.what()))); }
}
bool Window::save(const Config &config, bool stopRunning) {
    if (batch_.busy()) { showError(QStringLiteral("请先取消一键启动。")); return false; }
    try {
        store_.save(config);
        if (stopRunning) runner_.stopAll();
        config_ = config; loaded_ = true; refreshList(); statusBar()->showMessage(QStringLiteral("配置已保存"), 4000); return true;
    } catch (const std::exception &e) { showError(QString::fromUtf8(e.what())); return false; }
}
void Window::editProject(bool create) {
    if (batch_.busy()) return;
    if (!loaded_) { showError(QStringLiteral("请先修复或导入有效配置。")); return; }
    const auto *p = selected(); if (!create && (!p || runner_.running(p->id))) return;
    ProjectEditor editor(create ? Project{} : *p, this);
    if (editor.exec() != QDialog::Accepted) return;
    const auto project = editor.project(); auto config = config_; bool found = false;
    for (auto &item : config.projects) if (item.id == project.id) { item = project; found = true; break; }
    if (!found) config.projects.append(project);
    if (save(config, false)) { search_->clear(); filter_->setCurrentIndex(0); refreshList(project.id); }
}
void Window::removeProject() {
    if (batch_.busy()) return;
    const auto *p = selected(); if (!p) return; const auto id = p->id;
    if (!confirm(QStringLiteral("删除项目“%1”？运行中的项目会停止，项目目录和文件不会删除。").arg(p->name))) return;
    auto config = config_; for (int i = 0; i < config.projects.size(); ++i) if (config.projects[i].id == id) { config.projects.removeAt(i); break; }
    if (save(config, false)) runner_.stop(id);
}
void Window::startProject() {
    if (batch_.busy()) return;
    const auto *p = selected(); if (!p || runner_.running(p->id)) return;
    const auto project = *p; if (logs_.contains(project.id)) logs_[project.id]->clear(); renderers_[project.id].reset(); runner_.start(project);
}
void Window::appendOutput(const QString &id, const QString &text) {
    if (!logs_.contains(id)) { auto *document = new QTextDocument(this); document->setMaximumBlockCount(10000); document->setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); logs_.insert(id, document); }
    renderers_[id].append(logs_[id], text);
    if (selectedId() == id) {
        if (terminal_->document() != logs_[id]) terminal_->setDocument(logs_[id]);
        if (follow_->isChecked()) terminal_->verticalScrollBar()->setValue(terminal_->verticalScrollBar()->maximum());
    }
}
void Window::editStartupPorts() {
    if (!loaded_ || batch_.busy()) return;
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("启动端口")); dialog.resize(400, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = label(QStringLiteral("一键启动前检查并释放这些 TCP 端口。"), "muted"); layout->addWidget(hint);
    auto *list = new QListWidget; list->setObjectName("startupPortList"); layout->addWidget(list);
    for (const auto port : config_.startupPorts) new QListWidgetItem(QString::number(port), list);
    auto *row = new QHBoxLayout; auto *port = new QSpinBox; port->setRange(1, 65535); port->setValue(3000); row->addWidget(port);
    auto *add = button(QStringLiteral("添加")); auto *remove = button(QStringLiteral("移除")); row->addWidget(add); row->addWidget(remove); layout->addLayout(row);
    connect(add, &QPushButton::clicked, &dialog, [list, port] {
        const auto text = QString::number(port->value()); if (list->findItems(text, Qt::MatchExactly).isEmpty()) new QListWidgetItem(text, list);
    });
    connect(remove, &QPushButton::clicked, &dialog, [list] { delete list->takeItem(list->currentRow()); });
    auto *buttons = new QDialogButtonBox; buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole); buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
        auto config = config_; config.startupPorts.clear();
        for (int i = 0; i < list->count(); ++i) config.startupPorts.append(list->item(i)->text().toUShort());
        if (save(config, false)) dialog.accept();
    }); dialog.exec();
}
void Window::editJson() {
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("编辑 JSON 配置")); dialog.resize(820, 640);
    auto *layout = new QVBoxLayout(&dialog); auto *editor = new QPlainTextEdit;
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); editor->setPlainText(QString::fromUtf8(config_.json()));
    if (!loaded_) { QFile file(store_.path()); if (file.open(QIODevice::ReadOnly)) editor->setPlainText(QString::fromUtf8(file.readAll())); }
    layout->addWidget(editor); auto *error = label({}, "error"); error->setWordWrap(true); layout->addWidget(error);
    auto *buttons = new QDialogButtonBox; buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole); buttons->addButton(QStringLiteral("保存配置"), QDialogButtonBox::AcceptRole)->setObjectName("primary"); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
        try {
            const auto config = Config::parse(editor->toPlainText().toUtf8());
            if (runner_.count() && !confirm(QStringLiteral("保存配置会停止所有运行中的项目，是否继续？"))) return;
            if (save(config, true)) dialog.accept();
        } catch (const std::exception &e) { error->setText(QString::fromUtf8(e.what())); }
    }); dialog.exec();
}
void Window::importConfig() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入配置"), {}, "JSON (*.json)"); if (path.isEmpty()) return;
    try {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
        const auto config = Config::parse(file.readAll());
        if (confirm(QStringLiteral("导入将替换当前配置并停止运行中的项目。是否继续？"))) save(config, true);
    } catch (const std::exception &e) { showError(QString::fromUtf8(e.what())); }
}
void Window::exportConfig() {
    if (!loaded_) { showError(QStringLiteral("当前配置无效，请先修复。")); return; }
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出配置"), "project-config.json", "JSON (*.json)"); if (path.isEmpty()) return;
    try { ConfigStore::write(path, config_.json()); statusBar()->showMessage(QStringLiteral("配置已导出"), 4000); } catch (const std::exception &e) { showError(QString::fromUtf8(e.what())); }
}
void Window::queryPort() {
    if (!portQuery_->isEnabled()) return;
    queriedPort_ = quint16(port_->value()); port_->setEnabled(false); portQuery_->setEnabled(false); portKill_->setEnabled(false); portResult_->setText(QStringLiteral("正在查询…"));
    auto *watcher = new QFutureWatcher<PortResult>(this);
    connect(watcher, &QFutureWatcher<PortResult>::finished, this, [this, watcher] {
        lastPort_ = watcher->result(); watcher->deleteLater(); port_->setEnabled(true); portQuery_->setEnabled(true);
        if (!lastPort_.error.isEmpty()) { portResult_->setText(lastPort_.error); return; }
        QStringList lines;
        for (const auto &owner : lastPort_.owners) lines << QStringLiteral("%1  ·  PID %2").arg(owner.name).arg(owner.pid);
        portResult_->setText(lastPort_.occupied ? QStringLiteral("端口 %1 正在使用\n%2").arg(queriedPort_).arg(lines.isEmpty() ? QStringLiteral("无法读取进程信息，可能需要更高权限。") : lines.join('\n')) : QStringLiteral("端口 %1 空闲").arg(queriedPort_));
        portKill_->setEnabled(!lastPort_.owners.isEmpty());
    });
    watcher->setFuture(QtConcurrent::run([port = queriedPort_] { return Ports::query(port); }));
}
void Window::killPort() {
    if (quint16(port_->value()) != queriedPort_ || lastPort_.owners.isEmpty()) return;
    if (!confirm(QStringLiteral("结束端口 %1 上列出的进程？未保存的数据可能丢失。").arg(queriedPort_))) return;
    port_->setEnabled(false); portQuery_->setEnabled(false); portKill_->setEnabled(false);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const auto error = watcher->result(); watcher->deleteLater(); port_->setEnabled(true); portQuery_->setEnabled(true);
        if (!error.isEmpty()) { portResult_->setText(error); return; }
        statusBar()->showMessage(QStringLiteral("已结束占用进程"), 5000); queryPort();
    }); watcher->setFuture(QtConcurrent::run([port = queriedPort_, owners = lastPort_.owners] { return Ports::terminate(port, owners); }));
}
bool Window::confirm(const QString &text) { return QMessageBox::question(this, QStringLiteral("请确认"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes; }
void Window::showError(const QString &message) { statusBar()->showMessage(message, 10000); QMessageBox::warning(this, QStringLiteral("操作未完成"), message); }
void Window::closeEvent(QCloseEvent *event) {
    if (runner_.count() || batch_.busy()) {
        event->ignore();
        if (!closing_ && confirm(QStringLiteral("关闭并停止所有项目？待执行的启动操作也会取消。"))) { closing_ = true; setEnabled(false); batch_.stop(); }
        return;
    }
    QSettings settings(store_.path() + ".ui.ini", QSettings::IniFormat); settings.setValue("geometry", saveGeometry()); event->accept();
}
