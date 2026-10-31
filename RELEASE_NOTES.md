# Continuous Pinch v1.0.1

`v1.0.1` 是一次以“无需先点击浏览器即可缩放”为核心的体验更新，同时修复了自定义应用图标在新版本中的资源绑定问题。

---

## 本次更新重点

### 鼠标悬停即可缩放

在 `v1.0.0` 中，Continuous Pinch 会根据当前前台窗口判断是否允许缩放。

这意味着即使鼠标已经移动到 Chrome / Edge 上，如果浏览器没有焦点，仍然需要先点击浏览器。

`v1.0.1` 默认改为：

```text
鼠标移动到浏览器
→ 按住 Alt
→ 滚动滚轮
→ 直接触发 pinch zoom
```

无需先点击浏览器。

---

### 新增目标判定模式

设置界面新增：

```text
目标判定 / Target
```

提供两种模式：

```text
鼠标悬停窗口（推荐）
当前前台窗口
```

默认：

```text
鼠标悬停窗口
```

如果更喜欢旧版行为，可以切换回“当前前台窗口”。

---

### 悬停模式的判定方式

悬停模式会根据鼠标指针下的顶层窗口找到对应进程：

```text
GetCursorPos
→ WindowFromPoint
→ GetAncestor(GA_ROOT)
→ GetWindowThreadProcessId
→ 获取 EXE 名称
→ 应用白名单
```

默认白名单仍然是：

```text
chrome.exe
msedge.exe
```

因此即使其它程序当前拥有键盘焦点，只要鼠标悬停在白名单浏览器上，Continuous Pinch 就可以响应。

---

### 性能处理

进程查询不会放在 `WH_MOUSE_LL` 低级鼠标 Hook 中。

程序会缓存鼠标下的根窗口，只有目标根窗口发生变化时才重新查询进程名和白名单状态，从而避免高频 `OpenProcess` 调用。

---

### 修复应用 Logo

在加入悬停目标功能后的开发版本中，自定义图标资源绑定一度被遗漏。

`v1.0.1` 已恢复：

- 托盘图标使用 `IDI_APP_ICON`
- 设置窗口 `hIcon`
- 设置窗口 `hIconSm`
- Windows Resource 编译流程

项目仍使用：

```text
app.ico
app.rc
resource.h
```

并通过 `build.bat` 将图标编译进 `ContinuousPinch.exe`。

---

## 配置变化

新增：

```ini
[Applications]
TargetMode=0
```

取值：

```text
0 = 鼠标悬停窗口（默认）
1 = 当前前台窗口
```

已有用户如果配置文件中没有 `TargetMode`，程序会自动使用默认值 `0`，无需手动修改配置文件。

---

## 默认操作

```text
Alt + Wheel Up
    Zoom in

Alt + Wheel Down
    Zoom out, never below the tool's logical 100% baseline

Alt + Middle Click
    Soft reset to baseline

Ctrl + Alt + Middle Click
    Hard reset synthetic touchpad

Ctrl + Alt + Shift + F12
    Exit
```

---

## 升级建议

从 `v1.0.0` 升级时，可以直接替换 EXE。

如果你希望继续保持旧版“必须是前台应用才能响应”的行为，请在设置中将：

```text
目标判定 / Target
```

切换为：

```text
当前前台窗口
```

---

## 特别鸣谢 / Special Thanks

特别感谢 GitHub 用户 [@zhoulin12138](https://github.com/zhoulin12138) 对 Continuous Pinch 的关注与支持。

---

## Version

```text
v1.0.1
```

这是 Continuous Pinch 的第一个补丁版本更新。
