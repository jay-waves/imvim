# imvim

Windows 优先的轻量 Neovim GUI 原型。C++20 + Dear ImGui；通过 MessagePack-RPC 连接 `nvim --embed`，不嵌入终端模拟器。

## 架构

- Windows：Win32 + Direct3D 11，静态链接 ImGui，没有 SDL / GLFW / Qt 运行依赖。
- Linux：SDL3 + OpenGL 3.3，用于本地开发和验证。
- 共用：子进程管道、RPC、`ext_linegrid` 网格状态、ImGui 绘制和输入转换。
- 按事件重绘；空闲时等待。按键按住时才使用短周期唤醒实现重复输入。首版光标不闪烁。
- 接收队列约 4 MiB、单条未完成 RPC 16 MiB、粘贴 8 MiB 上限。保留待提交和已提交两份屏幕网格，不复制整个编辑文件。
- 依赖固定到 `vcpkg.json` 的 baseline；ImGui 1.93 使用按需生成字形的字体系统。

## 构建

要求：CMake >= 3.25、C++20 编译器、Ninja、全局 vcpkg；运行和集成测试需要 Neovim >= 0.11。使用 PATH 中的工具。

### Linux

Fedora 系统开发依赖（需要显式安装 X11 / Wayland 扩展头文件，不能只装 SDL3-devel）：

```sh
sudo dnf install gcc-c++ cmake ninja-build git curl zip unzip tar pkgconf-pkg-config \
  SDL3-devel mesa-libGL-devel autoconf autoconf-archive automake libtool xorg-x11-server-Xvfb \
  libXext-devel libXcursor-devel libXi-devel libXrandr-devel libXfixes-devel \
  libXScrnSaver-devel libXtst-devel wayland-devel libxkbcommon-devel libdecor-devel ibus-devel
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
export VCPKG_FORCE_SYSTEM_BINARIES=1
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
./build/linux/imvim --clean
```

仅编译协议和测试，不需要图形依赖：`cmake --preset core && cmake --build --preset core && ctest --preset core`。

### Windows

在 VS 2022 的 x64 Developer PowerShell 中，设置 `VCPKG_ROOT` 指向共享 vcpkg：

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
.\build\windows\imvim.exe --clean
```

`windows` preset 使用 `x64-windows-static` 和静态 MSVC CRT。Neovim 单独安装并加入 PATH，或使用 `--nvim C:\path\to\nvim.exe`。发布包不包含 Neovim 或字体。

`.github/workflows/build.yml` 在 Windows runner 原生编译、运行真实 Neovim 集成测试，并上传 `imvim-windows-x64.zip`。Linux job 另外运行 Xvfb 图形冒烟测试。推送到 `git@github.com:jay-waves/imvim.git` 后触发。

## 使用

```sh
imvim --clean                              # 隔离插件开销
imvim -- README.md                         # 加载正常 Neovim 配置
imvim --font /path/to/mono.ttf --font-size 18
imvim --fallback-font /path/to/cjk.ttf      # 缺字时查询这份字体；也支持常规 TTC
```

- 编辑、分屏、命令、插件仍由 Neovim 负责。
- 支持文本、导航键、Ctrl 组合键、鼠标点击/拖动/滚轮。
- `Ctrl+Shift+V` 使用系统剪贴板，通过 `nvim_paste` 粘贴。普通 `Ctrl+V` 保留 Neovim 原有行为。
- 关闭窗口向 Neovim 发出 `confirm qall`，有修改时在编辑区确认。`:q` / `:qa` 正常退出。
- Windows 使用 Unicode 命令行，支持含空格和中文的路径。

## 当前边界

这是用于验证功能与内存的原型，不是成熟 GUI：

- 默认只自动寻找基础等宽字体；中文需要指定包含中文字形的主字体或 `--fallback-font`。不做系统字体自动回退。
- 暂无连字、复杂文字 shaping、彩色 emoji、独立粗体/斜体字形；undercurl 暂画成直下划线。
- Windows 使用系统 IME 的基础提交及候选框定位接口；真实中文输入法、跨屏 DPI 和显卡行为需要 Windows 实机验证。Linux 暂未绘制组合输入的预编辑文本。
- Alt/AltGr、非美式键盘组合、平台快捷键仍需完善。
- `guifont` 变化不会动态更新 GUI 字体；请使用命令行字体选项。
- 没有外置补全菜单、外置命令行、multigrid、动画或自定义标题栏。
- GUI 不能降低 Neovim 插件和语言服务器本身的占用。
- Windows 子进程 stderr 暂定向到 NUL，GUI 显示 RPC / 启动错误；更完整的诊断日志待补。

## 验证和内存测量

```sh
ctest --preset linux
xvfb-run -a ./build/linux/imvim --smoke-seconds 5 \
  --stats build/linux/smoke.json --screenshot build/linux/smoke.ppm
xvfb-run -a python3 tools/measure-linux.py --output build/linux/memory.json -- \
  ./build/linux/imvim --smoke-seconds 10 --stats build/linux/idle.json
```

`--smoke-seconds` 强制使用 `--clean -n` 和临时缓冲区，自动填入测试文字并退出，不修改用户文件。`--screenshot` 当前仅支持 Linux，输出 PPM。字体选项也可用于冒烟测试。

内存脚本分别采样 GUI 和 Neovim 的 RSS、PSS、私有内存，单位 KiB；不包含 GPU 分配。Xvfb 下的软件 OpenGL 可能显著增加 GUI RSS，不能把这个数字当成 Windows DX11 占用。Windows 可运行 `powershell -File tools/measure-windows.ps1` 自动采样 GUI 和 Neovim；该脚本仍需 Windows 实机验证。请在同一机器、同一文件与配置下比较 GUI / nvim / LSP 各进程的 Private Bytes 与 Working Set，并单独记录 GPU 内存。

核心测试覆盖网格滚动、双宽字符、分片 MessagePack、真实 Neovim 输入与中文文件保存、缩放和退出。CI 通过不等于 Windows IME 或硬件图形验收通过。

## 初次实测

[Windows CI 构建](https://github.com/jay-waves/imvim/actions/runs/43730641469)（2026-11-15，Windows Server 2022 runner，1000×700 窗口，12 秒 `--clean` 测试，默认 Consolas，未加载中文回退字体）：

| 进程 | 结束前私有内存 | 结束前工作集 |
| --- | ---: | ---: |
| imvim GUI | 10.6 MiB | 29.4 MiB |
| Neovim | 7.5 MiB | 12.6 MiB |

这不是完整桌面编辑负载，也不是与终端的同机对照；不包含 GPU 内存、插件和 LSP。字体、驱动与真实配置都会影响结果。`--stats` 会记录实际渲染器，方便区分硬件驱动与软件渲染。
