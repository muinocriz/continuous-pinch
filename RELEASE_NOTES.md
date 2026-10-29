# Continuous Pinch v1.0.0

Continuous Pinch 的第一个公开版本。

这是一个面向 Windows 11 的鼠标滚轮捏合缩放工具。

它不会使用传统的：

```text
Ctrl + Wheel
```

而是将鼠标滚轮转换为 Windows 11 Synthetic Precision Touchpad 的 pinch gesture。

---

## Highlights

### 真正的 Synthetic Precision Touchpad

Continuous Pinch 使用 Windows 11：

```text
PT_TOUCHPAD
```

生成虚拟触控板输入。

输入链路：

```text
Mouse Wheel
→ Continuous Pinch
→ Synthetic Precision Touchpad
→ Pinch Gesture
→ Browser Viewport Zoom
```

---

### 100% 最低基准

默认只允许：

```text
100% → 放大
```

缩小时：

```text
放大状态 → 100%
```

不会继续缩到工具逻辑基准以下。

---

### 慢速滚轮支持

按住缩放修饰键时，即使你：

```text
滚一格
等一秒
再滚一格
```

仍然属于同一个连续 pinch session。

---

### 安全复位

Soft Reset：

```text
Alt + 中键
```

Hard Reset：

```text
Ctrl + Alt + 中键
```

Hard Reset 会销毁并重新创建 Synthetic Touchpad Device。

---

### 可配置界面

包括：

- 中文 / English
- 默认中文
- 系统托盘
- 灵敏度
- smoothing 曲线
- 热键
- 应用白名单
- 开机启动

---

## Default Controls

```text
Alt + Wheel Up
    Zoom in

Alt + Wheel Down
    Zoom out to the logical 100% baseline

Alt + Middle Click
    Soft reset

Ctrl + Alt + Middle Click
    Hard reset

Ctrl + Alt + Shift + F12
    Exit
```

---

## Default Application Whitelist

```text
chrome.exe
msedge.exe
```

---

## Requirements

- Windows 11
- Microsoft Edge or Google Chrome recommended

如果自行编译：

- Visual Studio 2022
- MSVC
- Windows SDK

---

## Build

使用：

```text
x64 Native Tools Command Prompt for VS 2022
```

运行：

```bat
build.bat
```

即可生成：

```text
ContinuousPinch.exe
```

---

## Known Limitations

Continuous Pinch 的 100% 是工具内部维护的逻辑基准。

如果在程序运行时使用其它设备或方式修改浏览器 viewport pinch scale，工具无法自动读取并同步该状态。

普通鼠标滚轮也是离散输入，因此无法完全复制真实触控板的连续采样手感。

---

## Version

```text
v1.0.0
```

这是首个 GitHub 正式发布版本。
