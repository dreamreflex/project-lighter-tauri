# DreamReflexLighter

基于 Tauri v2 + React + TypeScript 的桌面项目启动器。

## 构建产物说明

项目使用 Tauri 打包，默认会在对应平台产出安装包：

- Linux: `.deb`、`.rpm`、`.AppImage`
- Windows: `.msi`、`.exe`（取决于 Tauri/系统工具链配置）

构建产物目录（通用）：

- `src-tauri/target/release/bundle/`

## 前置要求

### 通用

- Node.js 18+（建议 20+）
- npm
- Rust（`rustup` + stable toolchain）

安装 Rust（如未安装）：

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source "$HOME/.cargo/env"
rustup default stable
```

安装前端依赖：

```bash
npm install
```

### Linux 额外依赖（Ubuntu/Debian）

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  pkg-config \
  libglib2.0-dev \
  libgtk-3-dev \
  libayatana-appindicator3-dev \
  librsvg2-dev \
  libwebkit2gtk-4.1-dev
```

### Windows 额外依赖

- 安装 [Visual Studio Build Tools（含 C++）](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
- 安装 [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)

## 本地构建

### 仅构建前端

```bash
npm run build
```

### 构建桌面应用（当前平台）

```bash
CI=false npm run tauri build
```

构建完成后，产物在：

- `src-tauri/target/release/bundle/`

## 同时构建 Windows 和 Linux（推荐 CI 并行）

由于 Tauri 原生打包依赖平台工具链，通常不能在一台 Linux 主机直接打包 Windows 安装包。推荐使用 GitHub Actions `matrix` 并行构建：Windows Runner 构建 Windows 产物，Linux Runner 构建 Linux 产物。

在仓库添加 `.github/workflows/build.yml`（示例）：

```yaml
name: Build Desktop

on:
  workflow_dispatch:
  push:
    tags:
      - "v*"

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-22.04, windows-latest]
    runs-on: ${{ matrix.os }}

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Setup Node
        uses: actions/setup-node@v4
        with:
          node-version: 20
          cache: npm

      - name: Setup Rust
        uses: dtolnay/rust-toolchain@stable

      - name: Install Linux system deps
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            build-essential \
            pkg-config \
            libglib2.0-dev \
            libgtk-3-dev \
            libayatana-appindicator3-dev \
            librsvg2-dev \
            libwebkit2gtk-4.1-dev

      - name: Install npm deps
        run: npm ci

      - name: Build tauri app
        run: npm run tauri build

      - name: Upload bundle artifacts
        uses: actions/upload-artifact@v4
        with:
          name: bundles-${{ runner.os }}
          path: src-tauri/target/release/bundle/**
```

这样一次触发会并行得到：

- `bundles-Windows`
- `bundles-Linux`

## 常见问题

- 报 `glib-2.0.pc` 缺失：说明 Linux 系统依赖未安装，先执行上面的 `apt-get install` 命令。
- 报 Rust toolchain 问题：执行 `rustup default stable`，再重试。
- 报权限相关问题：确认命令在当前项目目录执行，并确保系统依赖安装步骤使用了 `sudo`。
