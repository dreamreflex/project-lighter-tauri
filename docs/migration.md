# Qt 迁移说明

## 功能对照

| 旧版功能 | Qt 实现 |
| --- | --- |
| React 项目卡片、运行状态 | 原生 QListWidget 项目列表、详情、运行统计 |
| 新建 / 编辑 / 删除项目 | ProjectEditor、原生目录选择、可增删排序的步骤表格 |
| command / commands | Config 同时读取两种格式，保留顶层及项目扩展字段 |
| Tauri 配置读写 | 沿用原路径，QSaveFile 原子写入、首次保存备份 |
| 导入 / 导出 / 编辑 JSON / 刷新 | 配置页和刷新按钮；覆盖前确认、严格校验及错误提示 |
| PowerShell 命令流 | Windows PowerShell，同一个会话顺序执行；Linux /bin/sh |
| stdout / stderr 事件 | QProcess 异步输出，增量 UTF-8 解码与 QTextDocument 渲染 |
| ANSI 样式 | 标准/高亮/256/RGB 前景背景色、粗体、斜体、下划线 |
| 启动 / 停止多个项目 | Runner 按项目 ID 管理进程，防止重复启动 |
| 停止全部 / 退出确认 | 关闭窗口先确认，等待所有进程结束再退出 |
| 端口查询 / 杀进程 | Windows TCP 表 API；Linux lsof + ss；后台线程执行 |
| 仓库链接 / 版本 | 侧栏仓库入口、版本和 Git 提交号，命令行 --version |

## 进程行为

QProcess 的信号驱动日志更新，界面线程不等待命令完成。Windows 在执行用户命令前将 PowerShell 加入带 `KILL_ON_JOB_CLOSE` 的 Job Object；Linux 创建独立 session / process group。停止项目和 Shell 结束时清理所属子进程。显式自行脱离进程组的 Unix 守护进程不在此管理范围内。

命令在同一个 Shell 中执行，目录/环境变量变化保留至后续步骤。Windows 原生命令失败码与 PowerShell 异常会中断命令流；Linux 使用 `set -e`，条件表达式等行为遵循 POSIX shell 规则。程序输出按 UTF-8 解码，仍使用旧代码页的 Windows 工具可能需要自行设置输出编码。

端口结束操作会在后台重新查询端口，核对 PID 集合，避免使用明显过期的查询结果。权限不足、依赖缺失、查询超时不会被显示成“端口空闲”。不终止启动器自身。端口查询范围与旧版一致，为 TCP LISTEN。

## 数据和 UI

Qt UI 首选项存入配置旁边的 `.ui.ini`。配置锁为 `.lock`；备份为 `.pre-qt.bak`。这些文件不应提交到仓库。

配置重载、导入和 JSON 保存会在数据变化/覆盖时确认停止运行任务；保存失败保持当前界面数据。项目编辑在运行时禁用，防止正在执行的配置与显示不一致。

日志是输出查看器，不是完整终端模拟器：不提供 stdin、PTY 或任意光标定位。此限制与旧版一致。新增了跨分块 ANSI/UTF-8 处理、回车进度覆盖、复制、日志导出及容量上限。

## 技术参考

- [Qt QProcess](https://doc.qt.io/qt-6/qprocess.html)
- [Qt QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)
- [Qt 应用运行库部署](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)

Qt 构建移除了 WebView 和前端运行时，但未做与旧版的性能基准对照，因此不承诺具体性能提升比例。
