# Changelog

本项目遵循语义化版本思路：

```text
MAJOR.MINOR.PATCH
```

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
```

作为开发迭代编号。

GitHub 对外正式版本从：

```text
v1.0.0
```

开始。
