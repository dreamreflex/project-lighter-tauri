# Lighter · 项目启动器

C++17 + **Qt 6 Widgets** 原生多项目启动器。界面、配置和进程管理均由 Qt/C++ 实现，运行时不依赖 Node.js、Rust 或 WebView。

## 功能

- 项目新建、编辑、删除；原生工作目录选择器。
- 多步骤命令顺序执行，共享 Shell 环境；步骤失败即停止后续步骤。
- 多项目并行启动、停止、退出码反馈；关闭窗口前确认并清理子进程。
- 实时 stdout / stderr 日志、ANSI 颜色（含 256 色和 RGB）、中文 UTF-8 输出。
- 兼容旧版 JSON 配置；导入、导出、JSON 编辑、刷新及首次写入自动备份。
- TCP 端口查询（IPv4 / IPv6）、进程信息、确认后结束占用进程。
- 现代侧栏、状态概览、项目搜索与筛选、明暗主题、键盘快捷键。
- 日志独立保留，支持复制、保存、清空和关闭自动滚动；每项目最多约 10,000 行 / 100 万字符，避免长期运行时无限增长。

## 构建与启动

要求：CMake 3.21+、C++17 编译器、Qt 6.4+（Core / Gui / Widgets / Concurrent / Network / Test）。

### Linux（Ubuntu 24.04 / Debian 12 或更新版本）

```bash
sudo apt-get install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools lsof iproute2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/lighter
```

### Windows

安装 Visual Studio 2022 C++ 工具、CMake 和 Qt 6 的 MSVC 64 位版本。在开发者命令提示符中：

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release --parallel
# 使用 Qt 开发环境启动，或先部署运行库
C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe build/Release/lighter.exe
./build/Release/lighter.exe
```

Windows 使用系统 Windows PowerShell；Linux 使用 `/bin/sh`。配置文件格式跨平台兼容，命令语法及路径需要符合目标平台。命令是非交互执行，不提供交互式终端输入。对于持续运行的服务，后续步骤会在该服务退出后才执行；需要并发的服务应配置为不同项目。

## 旧数据迁移

默认直接读取 Tauri 版本使用的 `config.json`，无需手动搬迁：

| 平台 | 路径 |
| --- | --- |
| Windows | `%APPDATA%/com.dreamreflex.lighter/config.json` |
| Linux | `${XDG_CONFIG_HOME:-~/.config}/com.dreamreflex.lighter/config.json` |
| macOS（未验证） | `~/Library/Application Support/com.dreamreflex.lighter/config.json` |

首次保存时备份原文件至 `config.json.pre-qt.bak`，之后使用原子替换。解析失败不会覆盖原数据，可从「配置管理 → 编辑 JSON」修复。单个 `command` 与 `commands` 数组均可读取，保存时统一写入 `commands`。

可隔离测试或使用其他配置文件：

```bash
./build/lighter --config /path/to/config.json
```

同一个配置文件只允许一个 Qt 实例打开。迁移时请先关闭旧版启动器；旧版不参与 Qt 的文件锁。

## 测试与打包

```bash
ctest --test-dir build -C Release --output-on-failure
cd build
cpack -C Release
```

- Linux：生成 `.deb` 和 `.tar.gz`。DEB 自动声明 Qt 运行库依赖；tar 包需要目标机器预装对应 Qt 6 运行库，不是独立 AppImage。
- Windows：生成 ZIP 和 NSIS 安装包，自动部署 Qt DLL 和平台插件。制作 NSIS 包需要安装 NSIS。
- GitHub Actions：Linux / Windows 构建和测试；分支、PR、手动触发上传产物，`v*` 标签发布 Release 并生成来源证明。
- Qt 版本的发布格式为上述格式，不再生成旧版 MSI / AppImage。

测试覆盖配置兼容和校验、备份、ANSI 分块解析、实际进程执行、失败中止、子进程停止、端口查询及原生界面基本交互。Linux 已在本地验证；Windows 相关实现需由 Windows CI 和实机验收。

## 目录

- `native/`：Qt/C++ 应用源码。
- `tests/`：Qt Test 回归测试。
- `packaging/`：桌面入口。
- `docs/migration.md`：功能对照与实现说明。
- `legacy/tauri/`：迁移前的完整 Tauri 源码，保留作回溯参考；不参与 Qt 构建。

快捷键：`Ctrl+N` 新建项目、`Ctrl+F` 搜索、`F5` 刷新配置。
