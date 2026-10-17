# MouseTest 独立鼠标（触摸板）测试工具

MouseTest 是从 `FactoryTestCases` 的 `TouchPad` 模块抽出的独立 Win32/C++ x64 测试工具。当前测试对象是 Windows Precision Touchpad：用触摸板划过全屏网格，并完成配置要求的左键、右键次数。

- 源项目：`D:\Code\FactoryTestCases`，源模块 `src/modules/touch_pad`
- 源版本：`2.15.10.17`
- 本工具版本：`1.0.10.17`

## 使用

双击 `package\MouseTest.exe` 或直接运行：

```powershell
.\MouseTest.exe
```

窗口覆盖主显示器完整屏幕，窗口内隐藏鼠标指针：

1. 用触摸板划过中心网格；首次覆盖的格子变绿。
2. 按配置完成左键、右键点击次数；点击窗口任意位置均计入，不鉴别点击来源。
3. 全部必测格、左键和右键次数完成后自动 PASS，窗口关闭，程序退出。
4. `Esc` 或关闭窗口为 FAIL。没有整体超时，未完成时持续等待输入。

设备识别固定使用原始输入 HID 顶层集合 `UsagePage=0x0D`、`Usage=0x05`，不按设备索引、名称或鼠标设备数量猜测。找不到 Precision Touchpad 或注册 Raw Input 失败时 FAIL，日志记录具体 API/Win32 错误。

## 配置

程序目录下的 `config.ini` 为 UTF-8 明文；文件不存在时使用内置默认值：5 行 × 10 列、左键 1 次、右键 1 次。

```ini
[language]
Culture=zh-CN

[TouchPad]
RowCount=5
ColumnCount=10
LeftClickCount=1
RightClickCount=1
Finger=
```

- `Culture` 支持 `zh-CN`、`en-US`、`vi-VN`，不区分大小写。缺失时默认中文；非法值使启动 FAIL。
- `RowCount` / `ColumnCount` 成对解析。任一缺失、非正整数或总格数超过 10000 时，两者一起回退为 `5/10` 并记录 WARNING。
- `LeftClickCount` / `RightClickCount` 成对解析。任一缺失或不是非负整数时，两者一起变为 `0/0`，即关闭按键计数。
- `Finger` 是角落免测矩形，格式为 `角编号x宽x高`；`0/1/2/3` 分别表示左上、右上、左下、右下，`x`/`X` 均可，宽高单位为格数。非空但格式错误或超出网格时启动 FAIL。
- 节名、键名不区分大小写；未知键忽略。

配置错误只阻止本次测试启动，程序会显示错误并写日志，不会创建测试窗口。

## 日志和退出码

`MouseTest.log` 写在程序目录，UTF-8 追加。开始、设备、错误、结束时间、耗时和最终结果先在内存缓存，测试结束后一次写入；日志保存失败会弹窗并返回 3。

| 退出码 | 含义 |
| --- | --- |
| 0 | PASS，日志已保存 |
| 1 | FAIL，包括 Esc、关窗、配置错误、找不到触摸板、窗口异常 |
| 2 | CANCEL |
| 3 | 日志保存失败或程序无法启动 |

## 构建

要求 Windows x64、Visual Studio 2026 C++ 工具链、CMake，以及同级 `FluentUI/sdk` 原生静态 SDK。默认从 `..\FluentUI\sdk` 查找，可用参数覆盖：

```powershell
.\build-release.ps1
.\build-release.ps1 -RunTests
.\build-release.ps1 -FluentUISdkDirectory D:\Code\FluentUI\sdk
```

输出 `package\MouseTest.exe`、`config.ini`、`MouseTestHistory.txt` 和 `使用说明.txt`。EXE 静态链接 C++ 运行库和 FluentUI，运行只需要 Windows 系统 DLL。

## 验证状态

配置解析、网格/按键判定、隐藏窗口生命周期、Esc/关窗/取消和 PASS 流程已有定向测试；正式 EXE 编译、版本信息和三语界面文字已核对。真实 Precision Touchpad、左右物理按键、边缘覆盖、目标机型以及 100%–200% DPI 仍需目标机实机验证，当前状态为**代码完成未实测**。

行为细节见 `docs\BEHAVIOR_BASELINE.md`，本次验证范围见 `docs\releases\v1.0.10.17.md`。
