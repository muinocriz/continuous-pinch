# CLAUDE.md：给接手本项目的 AI 助手

这份文档写给在新对话中接手 Continuous Pinch 的 AI 助手。它记录了项目现状、工作方式和已经做过的决定，读完即可直接开工。面向用户的说明见 `README.md`，版本历史见 `CHANGELOG.md`；这里不重复它们，只写接手时需要知道、但从那两份文档里看不出来的东西。

最后更新：2026-11-07（v1.3.0 已发布，PR #1 ~ #11 全部合并）。

---

## 1. 关于项目所有者（最重要）

- GitHub 账号 **muinocriz**，用中文沟通，**本人不写代码**，项目全部由 AI 编写。所以：
  - 解释问题和方案时用通俗的话，少用术语；需要用户操作时，给出具体到按钮的步骤。
  - 用户负责在 **Windows 11 上实测**。每个 PR 都要附一份“需要你实测的项目”清单。
- **删除任何东西之前必须先征得同意**：文件、分支、截图、代码段，一律先问。这是用户明确提出的长期要求。
- 用户会对建议做取舍。下面“已讨论但暂不做”一节里的事项，除非用户重新提起，否则不要再主动推销。

---

## 2. 工作流程

1. 从最新的 `origin/master` 新建分支，一个任务一个分支、一个 PR，**目标分支永远是 `master`**。不要开“叠在另一个 PR 上”的 PR：曾经因此导致 v1.1.0 合进了错误的分支，只能补开 PR #3 修正。
2. 提交信息用英文写清楚原因与验证方式；PR 标题和正文用中文，正文包含：概述、改动、测试、需要用户实测的项目。
3. PR 由用户自己审阅和合并。合并后用 `gh api repos/muinocriz/continuous-pinch/pulls/<n>` 核对 `merged` 状态，并确认 master 的代码与分支一致。
4. 开始新任务前先 `git fetch`。用户有时会直接在 GitHub 网页上传文件（例如更新截图），不要覆盖这些改动。

### 本环境中的工具限制

- `git push` 可用（Claude GitHub App 已安装在该仓库）。
- `gh pr ...` 等命令**不可用**（GraphQL 接口被拦截），改用 REST：
  `gh api repos/muinocriz/continuous-pinch/pulls -f title=... -f head=<branch> -f base=master -F body=@body.md`
- 开启或修改 GitHub Pages 的 API 被拦截；这类仓库设置需要请用户手动操作。
- 网页抓取只允许访问用户提供的链接；需要看其他公开仓库时，可以直接 `git clone --depth 1`。

---

## 3. 没有 Windows 时如何验证

云端环境是 Linux。以下方法都已验证可用，但工具需要自行安装（会话之间不保留）：

| 要验证什么 | 方法 |
|---|---|
| 能否编译 | `apt install g++-mingw-w64-x86-64` 交叉编译。参数：`-std=c++17 -municode -DUNICODE -D_UNICODE -DNTDDI_VERSION=0x0A000006 -Wall -Wextra`。MinGW 的 user32 导入库缺少 `InjectSyntheticPointerInput` / `DestroySyntheticPointerDevice`，需要用一个 `.def` 文件和 `x86_64-w64-mingw32-dlltool` 生成导入库补上。用户实际使用 MSVC（`build.bat`）。 |
| 手势算法有没有改坏 | 用 Wine 运行一个测试程序：直接驱动 `Pinch.cpp`，再通过覆盖 `__imp_InjectSyntheticPointerInput` 等导入表指针，截获每一帧注入的手指坐标、`pointerFlags` 和 `dwTime`，与改动前的版本逐帧比对。v1.1 ~ v1.3 的引擎改动都这样验证过（5 种场景 × 4 种平滑曲线，共 834 帧完全一致）。**这个测试程序没有放进仓库**，需要时照此思路重建。 |
| 设置对话框的布局 | Wine 加 Xvfb 渲染对话框并截图；用 regedit 把 `Microsoft YaHei UI` 映射到 `WenQuanYi Micro Hei`，LogPixels 设为 144 可测试 150% 缩放。 |
| 项目主页 | 使用环境中预装的 Playwright + Chromium 截图并检查控制台。`?t=4.2` 可把演示动画定格在某一时刻，`?lang=en` 打开英文版。 |

两个踩过的坑：
- **Xvfb 必须用 `nohup setsid Xvfb :99 ... &` 常驻启动**。否则它会随那条命令一起退出，测试窗口全部创建失败、句柄为空，导致测试“假通过”。
- **不要用 `git worktree add <路径> master` 做验证**：它会检出本地 master 分支，接下来的 `git am` 会把本地 master 往前推。改用 `--detach origin/master`。

---

## 4. 代码架构

C++17、纯 Win32，没有第三方依赖。源码在 `src/`，按 `.clang-format` 统一格式（修改后执行 `clang-format -i src/*.cpp src/*.h`）。

### 原理

程序创建一块 **Windows 11 虚拟精确式触控板**（Synthetic Pointer API，`PT_TOUCHPAD`），把“Alt + 滚轮”换算成两根手指的间距变化后注入，浏览器就会做真正的触控板捏合缩放：视图以指针为中心放大，页面排版不变。macOS 可以直接发送“缩放增量”事件（LinearMouse 就是这么做的），Windows 没有这条路，所以必须模拟手指。

### 两个线程

- **界面线程**（`Main.cpp`）：托盘、设置对话框、配置读写、退出热键。只向输入线程投递命令和“配置已变更”通知，从不直接操作手势状态。
- **输入线程**（`InputThread.cpp`）：鼠标钩子、手势引擎、虚拟触控板都在这个线程上，因此输入侧是单线程的，不需要加锁。帧定时器是高精度 waitable timer，**只在手势进行时运行**（125 Hz），空闲时整个线程休眠。

### 模块

| 文件 | 职责 |
|---|---|
| `Common.h` | 公共头文件、全部常量（`Const` 命名空间）、配置类型 |
| `Config.*` | `g_cfg`（运行时配置，可能含设置窗口中尚未应用的预览值）与 `g_committedCfg`（已应用、会写入文件的配置）；INI 位置与 UTF-16 编码；开机启动注册表项（按路径判断是否属于当前 EXE） |
| `InputThread.*` | 线程主循环、帧定时器、配置快照、Alt 菜单屏蔽键 |
| `MouseHook.*` | `WH_MOUSE_LL` 钩子：只处理滚轮和中键，按“已启用 + 修饰键匹配 + 白名单窗口”决定是否拦截；把目标顶层窗口的 HWND 随消息一起传递 |
| `Target.*` | 判断鼠标下（或前台）的窗口是否属于白名单进程，按（HWND, PID）缓存 |
| `Pinch.*` | 手势状态机：目标缩放值、平滑曲线、手指间距换算、分段接力、软/硬复位、**按窗口记录逻辑基准**（`SelectZoomWindow`） |
| `Touchpad.*` | 创建/销毁虚拟触控板；注入帧（DOWN / UPDATE / UP 标志，真实时间戳） |
| `SettingsWindow.*` | 设置对话框（模板在 `app.rc` 的 `IDD_SETTINGS`）、退出热键注册 |
| `Tray.*` | 托盘图标与菜单；资源管理器重启后自动恢复图标 |

### 关键常量与行为（`Common.h`）

- 手指间距：基准 4200，范围 1000 ~ 8200。超出范围就让手指“抬起再按下”接力，称为分段。
- 灵敏度默认 `0.070`（对数值，约每格滚轮放大 7.3%）；单个滚轮事件最多折算 5 格。
- `UsePointerTransitionFlags = true`：如果某个 Windows 版本上缩放完全失效，改为 `false` 可恢复 v1.0.x 的帧标志写法。
- Alt 菜单屏蔽键：拦截 Alt + 滚轮 / 中键时注入 `vkE8`（与 AutoHotkey 的做法相同），每 0.25 秒最多一次。
- 看门狗：手指按下后 3 秒没有进展，自动收尾。

### 不要改的东西

- 名称里带 **“V3”的内部标识必须保留**：INI 文件名 `continuous_pinch_v3.ini`、开机启动注册表值 `ContinuousPinchV3`、主窗口类名、单实例互斥量。改名会让老用户的配置和开机启动失效，单实例检测也会在新旧版本之间失效。界面上显示的名称一律是 “Continuous Pinch”。
- INI 中的键名不能本地化。v1.0.x 曾把 `Enabled` 写成过中文 `启用`；`LoadConfig` 里有读取旧键的迁移逻辑，不要删。

### 输入线程消息协议（`Common.h`）

`WM_INPUT_WHEEL`：`LOWORD(wParam)` 为滚轮增量，`HIWORD(wParam)` 为标志位（`InputFlag_AltHeld`），`lParam` 为目标窗口 HWND。另外还有 `WM_INPUT_SOFT_RESET`、`WM_INPUT_HARD_RESET`、`WM_INPUT_CONFIG_CHANGED`、`WM_INPUT_END_GESTURE`。

---

## 5. 构建与发布

- 用户在 “x64 Native Tools Command Prompt for VS 2022” 中执行 `build.bat`。manifest 由 `app.rc` 嵌入，所以链接参数里有 `/MANIFEST:NO`。
- 常见编译问题：`LNK1104` 表示旧版程序还在运行；从旧目录解压覆盖升级时，残留的 `src\ContinuousPinch.cpp` 会导致重复定义（README 的“常见问题”已写明）。
- **发新版本时需要同步修改的地方**：
  1. `resource.h` 中的 `APP_VERSION_*`（会进入 EXE 文件属性和托盘提示）；
  2. `app.manifest` 中的 `assemblyIdentity version`；
  3. `CHANGELOG.md` 新增一节；
  4. `README.md` 的版本徽章和“版本”一节的要点表。
- 发布说明写在 **GitHub Releases** 页面，内容取自 CHANGELOG 中对应的一节（仓库里不再保留 RELEASE_NOTES.md）。标签统一写成 `vX.Y.Z`，Release 标题格式为 `Continuous Pinch vX.Y.Z`，并附上编译好的 `ContinuousPinch.exe`。
- EXE 没有代码签名，首次运行会出现 SmartScreen 提示，README 中已说明处理方法。

---

## 6. 文档与项目主页

- `README.md`：中文为主，末尾有英文摘要。修改功能时同步更新“功能特性”“默认操作”“设置窗口”“常见问题”等相关章节。
- `CHANGELOG.md`：按版本分节，条目用中文，分为 Changed / Fixed / Development。
- `docs/`：GitHub Pages 项目主页，地址 https://muinocriz.github.io/continuous-pinch/ ，从 master 的 `/docs` 目录发布。
  - 只有一个 `index.html`，不引用外部字体或脚本（照顾国内访问速度）；配色取自软件图标（深蓝底、青色强调）。
  - 首屏是三栏同步对比动画：触控板 / Continuous Pinch / Ctrl + 滚轮。时间轴常量为 `SPEED = 1.2`、`CYCLE = 10.4`，Ctrl 一栏用 CSS `zoom` 产生真实的重新排版。
  - 文案由脚本里的 `I18N` 字典提供，**默认中文**；新增文字时中英文都要加。
  - `docs/assets/` 中的设置截图是 `assets/screenshots/` 的副本，用户更新截图时两处要保持一致。
- 许可证：**MIT**（`LICENSE`，Copyright (c) 2026 muinocriz）。

---

## 7. 已知限制与讨论过的事项

### 已知限制

- 逻辑 100% 基准按浏览器**窗口**记录，同一窗口内的标签页共用一个；页面跳转后浏览器会自行把缩放重置为 100%，工具感知不到，需要用户按 Alt + 中键软复位。
- 浏览器以管理员身份运行时，本程序也必须以管理员身份运行（UIPI 限制）。
- 只适用于 Windows 11。

### 已讨论但用户决定暂不做（除非用户重新提起，不要主动推荐）

- 根据窗口标题变化，判断页面跳转或标签切换，并自动重置基准；
- GitHub Actions 自动编译、自动测试、自动发布；
- 代码签名证书；
- 缩放时在屏幕上显示倍数；每个应用单独设置灵敏度；
- 扩充默认白名单（Firefox、Brave 等，需要实测）。

### 已确认不需要改的

- 高精度滚轮：增量本来就按 `delta / 120` 折算，不足一格也按比例生效。
- 松开 Alt 时结束手势：手势进行中每 8 ms 检查一次，延迟可以忽略；不为此增加全局键盘钩子（会提高杀毒软件误报风险）。

### 可以建议给用户的小事

`assets/gifs/` 里的两个演示 GIF 合计约 11 MB，会拖慢 README 的加载。可以建议改用 MP4（在 GitHub 网页编辑器中拖入上传，视频不进仓库）或压缩 GIF。替换或删除文件前须先征得用户同意。
