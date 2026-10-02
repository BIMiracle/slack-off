# SlackOff

**Windows 11 轻量快捷隐藏 · 原生 C++ / Win32 桌面工具**

中文 | [English](README_EN.md)

[![Platform: Windows 11](https://img.shields.io/badge/Platform-Windows%2011-0078D4.svg)](https://github.com/BIMiracle/slack-off)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](CMakeLists.txt)
[![Portable](https://img.shields.io/badge/Portable-Single%20EXE-green.svg)](#安装与使用)

通过全局快捷键隐藏或恢复指定程序的所有可见窗口，也可在看剧时让光标移出窗口自动隐藏。隐藏和看剧快捷键支持区分左右 Ctrl、Alt；工具以单个 EXE 运行，无需安装 .NET、浏览器内核或 VC++ 运行库。

> 隐藏窗口不会停止后台音频、网络或通知，可配置隐藏前按键来暂停播放。第三方程序自身的托盘图标目前不支持隐藏。

## 主要功能

- **快捷隐藏与恢复**：按完整 EXE 路径匹配程序，控制该路径的所有运行实例；再按同一快捷键恢复窗口及原有位置。
- **左右快捷键**：隐藏 / 恢复与看剧模式分别记录左 Ctrl、右 Ctrl、左 Alt、右 Alt；同一个主键可以配合不同左右组合执行不同动作。
- **看剧模式**：先手动调整窗口大小，开启模式并将光标移入窗口后，首次移出可见外框即隐藏；恢复后等待光标重新进入。
- **隐藏前按键**：可发送空格等暂停键，并设置 0–500 ms 的延迟；恢复时不自动发送播放键。
- **托盘与自启**：关闭或最小化设置窗口后驻留托盘，支持当前用户登录自启、恢复全部和管理员重启。
- **恢复与持续监测**：正常退出先恢复全部；异常退出后下次启动尝试恢复。目标隐藏期间新出现或重新显示的窗口会继续隐藏。

## 安装与使用

### 1. 准备环境

运行环境为 **Windows 11 x64**。从源码构建需要 Visual Studio 2022 / 2026 的 **使用 C++ 的桌面开发**、Windows SDK 和 CMake；应用本身没有额外运行时依赖。

### 2. 构建并运行

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

脚本先构建 Release 并运行 CTest，随后将 EXE 与中英文说明复制到 `release\`。把 `release\SlackOff.exe` 放到固定位置后运行。首次运行显示设置，并默认注册当前用户的登录自启；登录启动只驻留托盘。

### 3. 设置程序与快捷键

1. 在窗口下拉框中选程序，点击 **选用程序**；也可点击 **浏览 EXE**。选择的是程序路径，会控制该路径的所有运行实例。
2. 点击 **隐藏 / 恢复快捷键** 输入框，按下所需组合，例如 `左Ctrl+左Alt+F9`。
3. 可选设置 **看剧模式快捷键**，例如 `右Ctrl+右Alt+F10`，以及 **隐藏前按键**，例如空格。点击 **保存程序**。
4. 按隐藏快捷键隐藏所有当前可见窗口，再按同一组合恢复；双击规则行也可切换。托盘右键菜单提供恢复全部和退出。
5. 看剧时先手动调整窗口大小，按看剧快捷键开启，再将光标移入目标窗口。光标移出可见外框即隐藏，再次按模式键关闭模式。

| 设置 | 默认值 / 行为 |
| --- | --- |
| 隐藏 / 恢复快捷键 | 未设置，保存程序前必须录入 |
| 看剧模式快捷键 | 未设置，可留空 |
| 隐藏前按键 | 未设置，可留空 |
| 按键后延迟 | `50 ms`，可设 `0–500 ms` |
| 恢复全部快捷键 | `Ctrl+Alt+Shift+F12`，左右均可，可在全局设置中修改 |
| 登录自启 | 首次正常运行时默认开启，当前用户权限 |

### 4. 左右 Ctrl / Alt 规则

隐藏和看剧输入框会显示实际录入的左右键，规则列表中也会保留该显示。Shift 仍不区分左右，界面不录入 Win 修饰键；按 Backspace / Delete 清空输入框，Tab / Shift+Tab 切换控件焦点。

| 录入的快捷键 | 触发行为 |
| --- | --- |
| `左Ctrl+左Alt+F9` | 只有该左右组合触发，改用右 Ctrl 或右 Alt 均不触发 |
| `右Ctrl+右Alt+F9` | 可与上一组合分别分配给隐藏和看剧，共用 F9 |
| 同时按左右 Ctrl 或 Alt 录入 | 触发时也必须按住录入的两侧 |
| 旧版保存的 `Ctrl+Alt+F9` | 继续接受任意一侧；重新录入并保存后才限定左右 |

录入时不会执行该输入框正在记录的快捷键。长按主键只触发一次，松开再按才再次触发。完全重复或匹配范围重叠的配置会提示错误，例如旧版通用 `Ctrl+Alt+F9` 与新的 `左Ctrl+左Alt+F9` 不能同时使用；保存失败时保留原有效快捷键。

Windows 的全局注册会预留整个 `Ctrl+Alt+主键` 组合，所以左右区分发生在 SlackOff 内部；其他程序占用了对应的通用组合，仍会导致注册失败。不要使用系统保留快捷键。恢复全部与隐藏前按键控件继续采用通用 Ctrl、Alt。

## 兼容边界

- 隐藏的是普通桌面窗口及其运行任务栏按钮。**第三方程序自身的托盘图标不支持隐藏**；工具自身有托盘入口。已固定的任务栏启动图标不移除。
- 不关闭、不挂起目标进程；后台音频、网络和通知需要目标程序自行暂停或关闭。
- 以完整 EXE 路径匹配，不支持按浏览器标签页隐藏。多个浏览器窗口都会隐藏；UWP、受保护窗口、独占全屏、特殊输入程序和跨权限窗口需要单独验证。
- 隐藏前按键只在手动隐藏和越界隐藏时发送。工具等待快捷键修饰键释放，最多 500 ms；只有确认目标已获得前台焦点才发送。无法确认时仍隐藏，并提示“按键发送未确认”。系统接受输入不保证播放器已暂停。
- 默认普通权限。控制管理员程序（包括以管理员身份启动的 scrcpy）时，点击 **以管理员身份重启** 并确认 Windows 提示后再使用快捷键。隐藏和开启看剧模式前会检查目标权限；权限不足时明确提示重启，不会进入无法隐藏的监测状态。重启前恢复已隐藏窗口；登录自启不会要求管理员权限。
- 使用物理屏幕坐标和 Per-Monitor V2 DPI 感知。鼠标 Hook 仅在看剧模式开启时安装；键盘 Hook 记录左右状态并处理快捷键录入，均无后台轮询。
- 恢复记录通过 PID、进程创建时间、路径、窗口类和窗口属性标记校验，避免错误恢复复用的句柄。原本隐藏的窗口不会被主动显示。
- Windows 窗口事件是异步的，新窗口可能短暂显示，无法保证所有程序完全无闪烁。

## 设置与自启

| 项目 | 位置 |
| --- | --- |
| 程序设置 | `%LOCALAPPDATA%\SlackOff\settings.ini` |
| 异常恢复记录 | `%LOCALAPPDATA%\SlackOff\recovery.ini` |
| 当前用户自启 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SlackOff` |

设置和恢复记录均先写临时文件，再原子替换。恢复记录包含程序路径和窗口身份，不包含窗口内容。设置格式 v2 保存快捷键的左右信息，并兼容旧版两字段快捷键。

自启值为 EXE 的绝对路径加 `--tray`。移动 EXE 后需关闭再开启自启以更新路径。移除工具前先关闭自启、从托盘退出，再删除 EXE；异常退出遗留的窗口可再次运行工具恢复。

## 构建与测试

使用静态运行库 `/MT`，Release 开启 `/O1`、LTCG 和未使用代码移除。

```powershell
pwsh -File build.ps1
pwsh -File tests/integration.ps1
pwsh -File tests/shortcuts.ps1
```

运行测试前请从托盘退出已有 SlackOff 实例，以释放全局快捷键。`build.ps1` 自动执行的 `--self-test` 覆盖配置保存、左右匹配、快捷键注册事务、多窗口隐藏、按键发送和恢复记录，不修改真实用户设置与自启。

`tests/integration.ps1` 验证应用状态机；`tests/shortcuts.ps1` 通过系统键盘输入验证左右组合、错误一侧不触发、同主键分配及长按去重。两者均使用隔离测试实例。自动注入键盘事件仍不能替代物理键盘验收。

额外验证：`pwsh -File tests/measure.ps1` 测量资源；`pwsh -File tests/real-apps.ps1` 验证实际程序，该脚本需要本机已有 FFmpeg / FFplay 来播放临时测试视频，并跳过已运行的同名用户程序。FFmpeg 不属于应用运行依赖。

底层报告位于 `%TEMP%\SlackOff-test-<PID>\results.txt`，应用测试报告位于 `%TEMP%\SlackOff-UI-test\`。`--test-instance` 使用隔离设置且不注册真实自启；`--fixture` 提供两个可见和一个原本隐藏的测试窗口。性能数据与验收边界见 [VALIDATION.md](VALIDATION.md)。

## 项目结构

```text
slack-off/
├── src/
│   ├── main.cpp            # 中文设置界面、托盘与应用状态机
│   ├── hotkeys.cpp         # 左右快捷键、键盘 Hook 与注册事务
│   ├── platform.cpp        # 窗口身份、按键发送与边界检测
│   ├── settings.cpp        # 原子持久化、自启与异常恢复
│   └── app.h               # 数据模型与模块接口
├── resources/              # Windows 资源与 DPI / 权限清单
├── tests/                  # 应用集成、快捷键、实际程序与资源测试
├── CMakeLists.txt          # MSVC / Windows SDK 构建配置
├── build.ps1               # 构建、CTest 与输出打包
├── README.md               # 中文说明（默认）
├── README_EN.md            # 英文说明
└── VALIDATION.md           # 实测结果与待确认的兼容边界
```

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=BIMiracle/slack-off&type=Date)](https://star-history.com/#BIMiracle/slack-off&Date)
