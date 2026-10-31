<p align="center">
  <img src="assets/app-logo.png" alt="Continuous Pinch Logo" width="160">
</p>

<h1 align="center">Continuous Pinch</h1>

<p align="center">
  Windows 11 鼠标滚轮模拟 Precision Touchpad 捏合缩放
</p>

<p align="center">
  <strong>Windows 11 · Synthetic Precision Touchpad · Mouse Wheel Pinch Zoom</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-11-0078D4?logo=windows11&logoColor=white" alt="Windows 11">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B&logoColor=white" alt="C++17">
  <img src="https://img.shields.io/badge/version-v1.0.1-brightgreen" alt="v1.0.1">
</p>

---

Continuous Pinch 是一个面向 Windows 11 的鼠标滚轮捏合缩放工具。

它将鼠标滚轮输入转换为 **Windows 11 Synthetic Precision Touchpad** 的 pinch gesture，让 Chrome / Edge 获得更接近真实触控板双指捏合的 viewport 缩放体验，而不是传统 `Ctrl + 滚轮` 的页面缩放。

> English: Continuous Pinch converts mouse-wheel input into Windows 11 synthetic Precision Touchpad pinch gestures for browser-style viewport zoom.

---

## 功能特性

- 使用 Windows 11 Synthetic Precision Touchpad (`PT_TOUCHPAD`)
- `Alt + 滚轮` 执行触控板式捏合缩放
- **默认按鼠标悬停窗口判定目标，无需先点击浏览器获取焦点**
- 可切换为“当前前台窗口”判定模式
- 应用白名单，默认支持 Chrome / Edge
- 向下缩小时不会低于工具维护的逻辑 100% 基准
- `Alt + 中键` 平滑回到逻辑 100% 基准
- `Ctrl + Alt + 中键` 硬复位 synthetic touchpad
- 支持慢速逐格滚轮，按住缩放修饰键时保持同一 gesture session
- Direct / Linear / Exponential / Adaptive 多种平滑曲线
- 实时灵敏度调节
- 热键与修饰键可配置
- 中文 / English 界面
- 系统托盘
- INI 配置持久化
- 开机启动
- 自定义应用图标

---

## 动图演示

### 1. 缩放功能演示

鼠标移动到白名单中的浏览器窗口上，按住 `Alt` 并滚动滚轮即可缩放。默认悬停模式下，浏览器不需要提前获得焦点。

<p align="center">
  <img src="assets/gifs/zoom-demo.gif" alt="Zoom Demo" width="900">
</p>

### 2. 中键复位功能演示

按住 `Alt` 并点击鼠标中键，可平滑回到工具记录的逻辑 100% 基准。

<p align="center">
  <img src="assets/gifs/reset-demo.gif" alt="Reset Demo" width="900">
</p>

---

## 软件截图

### 中文界面

![中文设置界面](assets/screenshots/settings-cn.png)

### English UI

![English settings](assets/screenshots/settings-en.png)

### 系统托盘

![系统托盘菜单](assets/screenshots/tray-menu.png)

---

## 默认操作

| 功能 | 默认操作 |
|---|---|
| 放大 | `Alt + 滚轮向上` |
| 缩小 | `Alt + 滚轮向下` |
| 回到逻辑 100% 基准 | `Alt + 中键` |
| 硬复位虚拟触控板 | `Ctrl + Alt + 中键` |
| 退出 | `Ctrl + Alt + Shift + F12` |

---

## 目标判定模式

Continuous Pinch 提供两种目标应用判定方式。

### 鼠标悬停窗口（默认，推荐）

默认模式下，程序根据**鼠标指针所在窗口**判断目标应用：

```text
鼠标位置
→ WindowFromPoint
→ GetAncestor(GA_ROOT)
→ 获取所属进程
→ 应用白名单
→ Synthetic Precision Touchpad pinch
```

因此即使其它程序当前拥有键盘焦点，只要鼠标移动到 Chrome / Edge 上，就可以直接使用：

```text
Alt + 滚轮
```

无需先点击浏览器。

### 当前前台窗口

如果切换为“当前前台窗口”，程序会使用当前获得焦点的顶层窗口进行白名单判断，行为与早期版本一致。

配置文件对应：

```ini
[Applications]
TargetMode=0
```

其中：

```text
0 = 鼠标悬停窗口
1 = 当前前台窗口
```

---

## 应用白名单

默认白名单：

```text
chrome.exe
msedge.exe
```

可以在设置窗口中修改，例如：

```text
chrome.exe
msedge.exe
brave.exe
```

只有白名单中的应用才会响应 Continuous Pinch。

在默认“鼠标悬停窗口”模式下，判断的是鼠标所在窗口所属的应用；在“当前前台窗口”模式下，判断的是当前前台应用。

---

## 100% 基准说明

Continuous Pinch 维护的是自己的逻辑缩放基准。

程序启动时：

```text
logicalScale = 1.0
```

工具允许：

```text
100% → 放大 → 缩小 → 回到 100%
```

但不会继续缩小到工具记录的 100% 以下。

如果程序运行期间使用真实触控板、浏览器其它手势或其它方式修改页面视觉缩放，Continuous Pinch 无法直接读取 Chromium 当前的 visual viewport scale，因此内部基准可能与页面实际状态产生偏差。

可以使用：

```text
Alt + 中键
```

执行软复位，或使用：

```text
Ctrl + Alt + 中键
```

执行硬复位。

---

## 慢速滚轮支持

只要缩放修饰键仍然按住，Continuous Pinch 会维持同一个 synthetic touchpad gesture session。

例如：

```text
按住 Alt
→ 滚一格
→ 等 1 秒
→ 再滚一格
→ 等 1 秒
→ 再滚一格
```

不会因为滚轮事件间隔较长而立即结束 pinch。

---

## Smoothing 模式

设置界面提供：

- Direct / immediate
- Linear
- Exponential
- Adaptive

默认推荐：

```text
Adaptive
```

如果希望响应更直接，可以尝试 Direct 或调整灵敏度。

---

## 系统要求

- Windows 11
- Visual Studio 2022 / MSVC（自行编译时）
- Windows SDK
- 支持 Precision Touchpad gesture 的目标应用

主要测试目标：

- Microsoft Edge
- Google Chrome

---

## 项目结构

```text
continuous-pinch/
├─ src/
│  ├─ Main.cpp               # 入口、主窗口消息、托盘命令
│  ├─ Common.h               # 公共头文件、常量、配置类型
│  ├─ Config.h/.cpp          # 配置状态、INI 读写、开机启动、本地化
│  ├─ Pinch.h/.cpp           # 捏合手势状态机：滚轮、平滑、分段、复位
│  ├─ Touchpad.h/.cpp        # Windows 11 Synthetic Precision Touchpad 设备
│  ├─ Target.h/.cpp          # 目标应用（白名单）判定
│  ├─ MouseHook.h/.cpp       # WH_MOUSE_LL 低级鼠标钩子
│  ├─ Tray.h/.cpp            # 托盘图标与菜单
│  ├─ SettingsWindow.h/.cpp  # 设置窗口、退出热键
│  ├─ Util.h/.cpp            # 时间、字符串、路径工具
│  └─ App.h                  # 全局窗口句柄
├─ assets/
│  ├─ app-logo.png
│  ├─ gifs/
│  │  ├─ zoom-demo.gif
│  │  └─ reset-demo.gif
│  └─ screenshots/
│     ├─ settings-cn.png
│     ├─ settings-en.png
│     └─ tray-menu.png
├─ app.ico
├─ app.rc
├─ resource.h
├─ build.bat
├─ README.md
├─ CHANGELOG.md
├─ RELEASE_NOTES.md
├─ .clang-format
├─ .gitattributes
└─ .gitignore
```

---

## 编译

推荐打开：

```text
x64 Native Tools Command Prompt for VS 2022
```

进入项目目录后：

```bat
build.bat
```

`build.bat` 会先编译 Windows 图标资源，再编译 `src` 下的全部源文件（中间文件输出到 `build\`）：

```bat
rc /nologo /fo build\app.res app.rc

cl /nologo /std:c++17 /EHsc /O2 /utf-8 /W4 ^
   /DUNICODE /D_UNICODE ^
   /Fobuild\ ^
   src\*.cpp ^
   build\app.res ^
   user32.lib comctl32.lib winmm.lib shell32.lib advapi32.lib gdi32.lib ^
   /Fe:ContinuousPinch.exe ^
   /link /SUBSYSTEM:WINDOWS
```

在 CI 等非交互环境中可以使用 `build.bat nopause` 跳过结尾的暂停。

代码格式由仓库根目录的 `.clang-format` 定义，修改代码后可执行：

```bat
clang-format -i src\*.cpp src\*.h
```

如果出现：

```text
LINK : fatal error LNK1104: 无法打开文件“ContinuousPinch.exe”
```

通常表示旧版 `ContinuousPinch.exe` 仍在运行。先从托盘退出程序或结束对应进程，再重新编译。

---

## 配置文件

程序运行后会在 EXE 同目录保存 INI 配置。

配置内容包括：

- 当前语言
- 是否启用
- 灵敏度
- smoothing 曲线
- 缩放/复位修饰键
- 退出热键
- 应用白名单
- **目标判定模式（悬停 / 前台）**
- 开机启动

> 当前配置文件名保留了开发阶段的历史命名。

---

## 托盘菜单

程序启动后常驻 Windows 系统托盘，可以：

- 启用 / 禁用
- 打开设置
- 回到逻辑 100% 基准
- 硬复位 synthetic touchpad
- 设置开机启动
- 退出

双击托盘图标可以直接打开设置窗口。

---

## 工作原理

传统浏览器页面缩放：

```text
Mouse Wheel
→ Ctrl + Wheel
→ Browser Page Zoom
```

Continuous Pinch：

```text
Mouse Wheel
→ Continuous Pinch
→ Windows Synthetic Precision Touchpad
→ PT_TOUCHPAD
→ Pinch Gesture
→ Browser Viewport Zoom
```

目标是模拟真正的触控板捏合手势，而不是浏览器页面缩放百分比快捷键。

---

## 已知限制

- 仅支持 Windows 11。
- 工具维护自己的逻辑 100% 基准，无法直接同步其它设备产生的 viewport pinch scale。
- 普通机械滚轮属于离散输入，手感无法与真实高采样率触控板完全一致。
- 不同鼠标可能需要不同灵敏度设置。
- 不同应用对 synthetic touchpad gesture 的支持程度可能不同。
- 悬停模式依据鼠标所在顶层窗口所属进程进行白名单判断。

---

## 版本

当前公开版本：

```text
v1.0.1
```

### v1.0.1 更新摘要

- 新增“目标判定模式”设置。
- 默认使用“鼠标悬停窗口”模式：鼠标移动到白名单浏览器上即可直接缩放，无需先点击浏览器获取焦点。
- 可切换回“当前前台窗口”模式，以保留旧版行为。
- 目标判定统一为顶层窗口 → 进程 → 白名单的流程。
- 修复新增悬停模式后自定义应用 Logo / 托盘图标丢失的问题。
- README、Changelog 与 Release Notes 同步更新。

内部开发过程中使用过 V1 / V2 / V3.x 等开发编号；GitHub 正式发布版本从 `v1.0.0` 开始。

---

## 特别鸣谢 / Special Thanks

特别感谢 GitHub 用户 **[@zhoulin12138](https://github.com/zhoulin12138)** 对项目的关注与支持。

Special thanks to GitHub user **[@zhoulin12138](https://github.com/zhoulin12138)** for the support and interest in Continuous Pinch.

---

## English Summary

Continuous Pinch converts mouse-wheel input into Windows 11 Synthetic Precision Touchpad pinch gestures.

Key features:

- Windows 11 `PT_TOUCHPAD` injection
- Mouse-wheel pinch zoom
- **Hover-target mode by default — browser focus is not required**
- Optional foreground-window target mode
- Application whitelist
- Logical 100% minimum baseline
- Slow single-notch wheel support
- Soft reset and hard reset
- System tray
- Chinese / English UI
- Configurable hotkeys
- Multiple smoothing curves
- Real-time sensitivity adjustment
- Start with Windows

Default controls:

```text
Alt + Wheel Up
    Zoom in

Alt + Wheel Down
    Zoom out, never below the tool's logical baseline

Alt + Middle Click
    Return to baseline

Ctrl + Alt + Middle Click
    Hard reset

Ctrl + Alt + Shift + F12
    Exit
```

---

## License

暂未选择开源许可证。
