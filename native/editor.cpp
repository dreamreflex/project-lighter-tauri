#include "editor.h"
#include <QtWidgets>
ProjectEditor::ProjectEditor(const Project &project, QWidget *parent) : QDialog(parent), original_(project) {
    setWindowTitle(project.id.isEmpty() ? QStringLiteral("新建项目") : QStringLiteral("编辑项目"));
    setMinimumSize(620, 470); resize(740, 540);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(28, 24, 28, 24); layout->setSpacing(16);
    auto *title = new QLabel(windowTitle()); title->setObjectName("sectionTitle"); layout->addWidget(title);
    auto *form = new QFormLayout;
    type_ = new QComboBox; type_->setObjectName("projectTypeInput");
    type_->addItem(QStringLiteral("命令"), "command"); type_->addItem(QStringLiteral("脚本"), "script");
    type_->setCurrentIndex(project.type == "script" ? 1 : 0);
    name_ = new QLineEdit(project.name); name_->setObjectName("projectNameInput"); name_->setPlaceholderText(QStringLiteral("项目名称"));
    directory_ = new QLineEdit(project.workingDir); directory_->setPlaceholderText(QDir::currentPath());
    auto *directoryRow = new QHBoxLayout; directoryRow->addWidget(directory_);
    auto *browse = new QPushButton(QStringLiteral("选择目录…")); directoryRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] { const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择工作目录"), directory_->text()); if (!path.isEmpty()) directory_->setText(path); });
    form->addRow(QStringLiteral("项目名称 *"), name_); form->addRow(QStringLiteral("类型"), type_); form->addRow(QStringLiteral("工作目录"), directoryRow); layout->addLayout(form);
    auto *hint = new QLabel(QStringLiteral("同一 Shell 顺序执行，失败中止。")); hint->setObjectName("muted"); hint->setWordWrap(true); layout->addWidget(hint);
    commands_ = new QTableWidget(0, 2); commands_->setObjectName("commandsInput");
    commands_->setHorizontalHeaderLabels({QStringLiteral("步骤名称（可选）"), QStringLiteral("执行命令 *")});
    commands_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    commands_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    commands_->setSelectionBehavior(QAbstractItemView::SelectRows); commands_->setAlternatingRowColors(true);
    layout->addWidget(commands_, 1);
    script_ = new QPlainTextEdit(project.script); script_->setObjectName("scriptInput");
    script_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); script_->setTabStopDistance(32);
#ifdef Q_OS_WIN
    script_->setPlaceholderText(QStringLiteral("PowerShell 脚本"));
#else
    script_->setPlaceholderText(QStringLiteral("Shell 脚本（/bin/sh）"));
#endif
    layout->addWidget(script_, 1);
    for (const auto &command : project.commands) addCommand(command);
    if (commands_->rowCount() == 0) addCommand();
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("＋ 添加步骤")); auto *remove = new QPushButton(QStringLiteral("删除步骤"));
    auto *up = new QPushButton(QStringLiteral("上移")); auto *down = new QPushButton(QStringLiteral("下移"));
    actions->addWidget(add); actions->addWidget(remove); actions->addStretch(); actions->addWidget(up); actions->addWidget(down);
    auto *commandActions = new QWidget; commandActions->setLayout(actions); layout->addWidget(commandActions);
    auto updateType = [this, hint, commandActions] {
        const bool isScript = type_->currentData().toString() == "script";
        commands_->setVisible(!isScript); commandActions->setVisible(!isScript); hint->setVisible(!isScript); script_->setVisible(isScript);
    };
    connect(type_, &QComboBox::currentIndexChanged, this, updateType); updateType();
    connect(add, &QPushButton::clicked, this, [this] { addCommand(); });
    connect(remove, &QPushButton::clicked, this, [this] { if (commands_->rowCount() > 1 && commands_->currentRow() >= 0) commands_->removeRow(commands_->currentRow()); });
    auto move = [this](int offset) {
        const int row = commands_->currentRow(), next = row + offset;
        if (row < 0 || next < 0 || next >= commands_->rowCount()) return;
        for (int column = 0; column < 2; ++column) {
            auto *a = commands_->takeItem(row, column), *b = commands_->takeItem(next, column);
            commands_->setItem(row, column, b); commands_->setItem(next, column, a);
        }
        commands_->selectRow(next);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); }); connect(down, &QPushButton::clicked, this, [move] { move(1); });
    error_ = new QLabel; error_->setObjectName("error"); error_->setWordWrap(true); layout->addWidget(error_);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    auto *save = buttons->addButton(QStringLiteral("保存项目"), QDialogButtonBox::AcceptRole); save->setObjectName("primary");
    connect(buttons, &QDialogButtonBox::accepted, this, &ProjectEditor::validate); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
    name_->setFocus();
}
void ProjectEditor::addCommand(const Command &command) {
    const int row = commands_->rowCount(); commands_->insertRow(row);
    commands_->setItem(row, 0, new QTableWidgetItem(command.name)); commands_->setItem(row, 1, new QTableWidgetItem(command.command));
    commands_->setRowHeight(row, 42);
}
Project ProjectEditor::project() const {
    Project result = original_;
    if (result.id.isEmpty()) result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.name = name_->text().trimmed(); result.workingDir = directory_->text().trimmed(); result.commands.clear();
    result.type = type_->currentData().toString(); result.script.clear();
    if (result.type == "script") result.script = script_->toPlainText();
    else for (int row = 0; row < commands_->rowCount(); ++row) result.commands.append({commands_->item(row, 0)->text().trimmed(), commands_->item(row, 1)->text().trimmed()});
    return result;
}
void ProjectEditor::validate() {
    // Commit an active cell editor before reading its model.
    commands_->setFocus();
    const auto value = project();
    if (value.name.isEmpty()) { error_->setText(QStringLiteral("请填写项目名称。")); return; }
    if (value.type == "script" && value.script.trimmed().isEmpty()) { error_->setText(QStringLiteral("脚本不能为空。")); return; }
    for (const auto &command : value.commands) if (command.command.isEmpty()) { error_->setText(QStringLiteral("请填写每一步的命令，或删除空白步骤。")); return; }
    accept();
}
