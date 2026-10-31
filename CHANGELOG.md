# Changelog

本项目遵循语义化版本思路：

```text
MAJOR.MINOR.PATCH
```

---

## v1.0.1

Hover targeting update.

### Added

- 新增“目标判定模式 / Target”设置。
- 新增“鼠标悬停窗口”目标判定模式，并设为默认。
- 新增“当前前台窗口”兼容模式，可恢复 v1.0.0 的目标判定行为。
- 新增 INI 配置项：

```ini
[Applications]
TargetMode=0
```

其中：

```text
0 = 鼠标悬停窗口
1 = 当前前台窗口
```

### Changed

- 默认目标应用判定从“当前前台窗口”改为“鼠标指针所在窗口”。
- 悬停模式下，不再要求 Chrome / Edge 先获取焦点。
- 目标识别流程改为：`WindowFromPoint → GetAncestor(GA_ROOT) → PID → EXE → 白名单`。
- 将运行时白名单判断统一为 `RefreshTargetAllowed()`，同时支持 Hover 与 Foreground 两种模式。
- 对鼠标悬停目标进行根窗口缓存，仅在目标根窗口变化时重新查询进程，避免在低级鼠标 Hook 中执行昂贵的进程查询。
- 设置修改后会立即重新检查当前目标，无需重启程序。

### Fixed

- 修复加入悬停目标模式后，自定义 `app.ico` 未正确显示的问题。
- 恢复托盘图标对 `IDI_APP_ICON` 的资源绑定。
- 恢复设置窗口 `hIcon` / `hIconSm` 的自定义图标绑定。
- 保持 Windows Resource (`app.rc` / `resource.h`) 与 `build.bat` 编译流程兼容。

### Documentation

- README 更新到 `v1.0.1`。
- README 增加目标判定模式说明。
- README 明确默认悬停模式不需要浏览器获取焦点。
- README 保留 Logo、GIF 演示和软件截图区域。
- 增加对 GitHub 用户 [@zhoulin12138](https://github.com/zhoulin12138) 的特别鸣谢。
- 更新 `RELEASE_NOTES.md`。

---

## v1.0.0

First public release.

### Added

- Windows 11 Synthetic Precision Touchpad 支持
- 使用 `PT_TOUCHPAD` 注入真正的触控板 gesture
- `Alt + 滚轮向上` 放大
- `Alt + 滚轮向下` 缩小
- 最低限制在工具记录的 100% 基准
- `Alt + 中键` Soft Reset
- `Ctrl + Alt + 中键` Hard Reset
- `Ctrl + Alt + Shift + F12` 退出
- 慢速逐格滚轮支持
- 高刷新率 pinch 更新
- 多种 smoothing 曲线
- Direct / Linear / Exponential / Adaptive
- 灵敏度实时调节
- 系统托盘
- 中文 / English 界面
- 默认中文
- 热键配置
- 应用白名单
- Chrome / Edge 默认白名单
- INI 配置保存
- 开机启动
- 自定义 EXE / 托盘图标支持
- Windows Resource 编译支持
- `build.bat` 一键编译

### Changed

- 从早期 synthetic touchscreen 方案迁移到 Windows 11 Synthetic Precision Touchpad
- 重构 pinch session 生命周期
- 缩放逻辑改为只允许从 100% 基准向上
- 优化慢速滚轮场景
- 优化连续 pinch 的跟手性和平滑度

### Fixed

- 修复早期 Touch Injection pointer ID 错误
- 修复缩小方向识别问题
- 修复 synthetic touchscreen 导致的页面平移问题
- 修复慢速逐格滚轮无响应问题
- 修复资源图标 Win32 Unicode 类型兼容问题
- 修复 WinMM / GDI 链接依赖问题
- 修复中英文界面本地化函数声明顺序问题

### Notes

内部开发阶段曾使用：

```text
V1
V2
V2.1
V3
V3.1
V3.2
V3.3
V3.4
```

作为开发迭代编号。

GitHub 对外正式版本从：

```text
v1.0.0
```

开始。
