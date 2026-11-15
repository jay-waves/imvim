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
  SDL3-devel mesa-libGL-devel autoconf autoconf-archive automake libtool fontconfig xorg-x11-server-Xvfb \
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

`windows` preset 使用 `x64-windows-static` 和静态 MSVC CRT。Neovim 单独安装并加入 PATH，或使用 `--nvim C:\path\to\nvim.exe`。发布包不包含 Neovim；包含中文与 Nerd Font 回退字体。解压时请保留 `fonts/` 目录和许可证文件，不能只复制 exe。

`.github/workflows/build.yml` 在 Windows runner 原生编译、运行真实 Neovim 集成测试，并上传 `imvim-windows-x64.zip`。Linux job 另外运行 Xvfb 图形冒烟测试。推送到 `git@github.com:jay-waves/imvim.git` 后触发。

## 使用

```sh
imvim --clean                              # 隔离插件开销
imvim -- README.md                         # 加载正常 Neovim 配置
imvim --font /path/to/mono.ttf --font-size 16
imvim --fallback-font /path/to/cjk.ttf      # 可选：覆盖内置中文回退字体；支持常规 TTC
```

- 编辑、分屏、命令、插件仍由 Neovim 负责。
- 支持文本、导航键、Ctrl 组合键、鼠标点击/拖动/滚轮。
- `Ctrl+Shift+V` 使用系统剪贴板，通过 `nvim_paste` 粘贴。普通 `Ctrl+V` 保留 Neovim 原有行为。
- 关闭窗口向 Neovim 发出 `confirm qall`，有修改时在编辑区确认。`:q` / `:qa` 正常退出。
- Windows 使用 Unicode 命令行，支持含空格和中文的路径。

## 配置

读取 `~/.config/imvim/config.json`；Windows 对应 `%USERPROFILE%\.config\imvim\config.json`。修改后重启生效。文件缺省时使用默认值，不自动写入用户配置。也可用 `--config FILE` 指定另一份配置。

Windows 示例（发布包附带 `config.example.json`，复制到上述目录并改名即可）：

```json
{
  "font": {
    "family": "Consolas",
    "fallback_families": ["Segoe UI Symbol"],
    "size": 16,
    "cjk_scale": 1.0,
    "line_spacing": 0,
    "letter_spacing": 0
  },
  "window": {
    "width": 1100,
    "height": 750,
    "maximized": false
  },
  "nvim": {
    "path": "nvim",
    "args": []
  }
}
```

| 字段 | 默认值 / 说明 |
| --- | --- |
| `font.family` | 空，自动选择等宽字体。Windows 用 DirectWrite，Linux 用 PATH 中的 `fc-match` 查询。Windows 找不到字体会报错；Linux 使用 Fontconfig 的匹配和替代规则。 |
| `font.file` | 空，也可按路径选择字体；与非空 `family` 互斥。 |
| `font.fallback_families` | `[]`，按顺序查询缺字；后面继续使用内置 Nerd Font、中文字体和系统符号兜底。字体集合保留所选 face 索引。 |
| `font.fallback_file` | 空，内置中文回退；非空则替换内置中文字体。 |
| `font.size` | `16`，范围 8–72。 |
| `font.cjk_scale` | `1.0`，范围 0.5–2；在自动两列匹配基础上微调双宽字形，过大时限制在行高内。 |
| `font.line_spacing` | `0`，额外行距，范围 -2–32。 |
| `font.letter_spacing` | `0`，额外列距，范围 -2–16；负值可能裁切字形。 |
| `window.width` / `height` | `1000` / `700`，初始客户区逻辑像素；范围分别 320–8192 / 200–8192。Windows 按初始 DPI 缩放。 |
| `window.maximized` | `false`，启动时最大化。 |
| `nvim.path` | `"nvim"`，裸命令名走 PATH；也可填写完整路径。 |
| `nvim.args` | `[]`，参数数组，保留空格，无 shell 展开；排在命令行传给 Neovim 的参数之前。不要指定 `--embed`、`--headless`、`--listen` 或 `--` 等传输选项。 |

Linux 可将 `family` 改为 `"monospace"`，将 `fallback_families` 留空或使用已安装字体名。系统字体查询不等于系统文字渲染：正文仍由 ImGui 绘制，尚未接入 DirectWrite 排版。

优先级：默认值 → JSON → 命令行。`--font FILE` / `--font-family NAME` 互相覆盖，后者生效；`--fallback-font`、`--font-size`、`--nvim` 覆盖对应配置。字体与 Neovim 路径支持 `~/`，相对路径以配置文件目录为基准；`nvim.path` 中不含路径分隔符的命令名保持 PATH 查找。Windows JSON 路径建议使用 `/`。所有字段可省略；未知字段、错误类型和非法范围会报告配置路径及原因。

窗口位置和尺寸暂不自动保存。`--smoke-seconds` 使用配置中的字体和窗口设置，但强制以 `--clean -n` 启动 Neovim，以便测量结果可比较。

## 当前边界

这是用于验证功能与内存的原型，不是成熟 GUI：

- 默认使用系统等宽主字体，自动合并随包提供的 Droid 中文回退与 Nerd Font Mono 图标；Windows 另查询系统 Segoe UI Symbol 补齐常用符号。默认字号 16，网格使用实际字形 advance，不逐格向上取整；双宽字符按两列英文宽度匹配字体尺寸，并在行内垂直居中。
- 两份回退字体合计约 6.4 MiB 文件大小，只按需生成字形；不保证覆盖所有 Unicode、生僻 CJK 扩展字符或 emoji。
- 暂无连字、复杂文字 shaping、彩色 emoji、独立粗体/斜体字形；undercurl 暂画成直下划线。
- Windows 使用系统 IME 的基础提交及候选框定位接口；真实中文输入法、跨屏 DPI 和显卡行为需要 Windows 实机验证。Linux 暂未绘制组合输入的预编辑文本。
- Alt/AltGr、非美式键盘组合、平台快捷键仍需完善。
- `guifont` 变化不会动态更新 GUI 字体；请使用 JSON 或命令行字体选项。
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

Linux 另有 `xvfb-run -a python3 tests/gui_x11.py build/linux/imvim`，通过 X11 原生按键输入文字、执行 `ZZ` 并验证保存结果。

GUI 冒烟测试也会检查中文、常用符号、BMP 与补充平面的 Nerd Font 图标是否存在真实字形。

核心测试覆盖网格滚动、双宽字符、分片 MessagePack、真实 Neovim 输入与中文文件保存、缩放和退出。CI 通过不等于 Windows IME 或硬件图形验收通过。

## 首版实测（加入默认回退字体之前）

[Windows CI 构建](https://github.com/jay-waves/imvim/actions/runs/43730641469)（2026-11-15，Windows Server 2022 runner，1000×700 窗口，12 秒 `--clean` 测试，默认 Consolas，未加载中文回退字体）：

| 进程 | 结束前私有内存 | 结束前工作集 |
| --- | ---: | ---: |
| imvim GUI | 10.6 MiB | 29.4 MiB |
| Neovim | 7.5 MiB | 12.6 MiB |

这不是完整桌面编辑负载，也不是与终端的同机对照；不包含 GPU 内存、插件和 LSP。字体、驱动与真实配置都会影响结果。`--stats` 会记录实际渲染器，方便区分硬件驱动与软件渲染。
