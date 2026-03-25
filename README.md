# 项目启动器

基于 **Tauri v2 + React + TypeScript** 的多项目一键启动管理工具。

[![Build & Release](https://github.com/dreamreflex/lighter/actions/workflows/release.yml/badge.svg)](https://github.com/dreamreflex/lighter/actions/workflows/release.yml)

## 下载

前往 [Releases](https://github.com/dreamreflex/lighter/releases) 下载对应平台的安装包：

| 平台 | 文件 |
|------|------|
| Windows | `*_x64-setup.exe`（NSIS 安装包）或 `*_x64_en-US.msi` |
| Linux | `*_amd64.deb`（Debian/Ubuntu）或 `*_amd64.AppImage` |

> Windows 10 1903+ / Windows 11 自带 WebView2，无需额外安装。

## 本地开发

### 前置要求

- Node.js 20+
- Rust（stable）

```bash
# 安装 Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source "$HOME/.cargo/env"
```

Linux 还需要安装系统依赖：

```bash
sudo apt-get install -y \
  libwebkit2gtk-4.1-dev libappindicator3-dev librsvg2-dev patchelf
```

### 启动开发服务器

```bash
npm install
npm run tauri dev
```

### 构建当前平台产物

```bash
npm install
npm run tauri build
```

产物位于 `src-tauri/target/release/bundle/`。

## CI / CD

项目已配置 GitHub Actions（`.github/workflows/release.yml`）：

- **推送 tag**（如 `v1.0.1`）→ 自动编译 Linux + Windows 并发布 Release
- **手动触发** → 编译产物作为 Artifact 上传，可在 Actions 页面下载（保留 7 天）
