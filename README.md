# MouseTest 独立鼠标 / 触摸板测试工具

MouseTest 是从 `FactoryTestCases` 的 `TouchPad` 模块抽出并扩展的独立 Win32/C++ x64 测试工具。现在包含两类测试：

1. **TouchPad**：Windows Precision Touchpad 划格覆盖 + 左右键计数。
2. **Mouse**：普通鼠标的滑动、滚轮、左键单击/双击、右键、滚轮点击和按住左键拖动。

- 源项目：`D:\Code\FactoryTestCases`，源模块 `src/modules/touch_pad`
- 源版本：`2.15.10.17`
- 本工具版本：`1.3.10.17`
- 公开源码：https://github.com/sclliang/MouseTest
- 下载发布包：https://github.com/sclliang/MouseTest/releases/latest

## 使用

双击 `package\MouseTest.exe` 或直接运行：

```powershell
.\MouseTest.exe
```

`config.ini` 中的 `Enabled` 决定运行哪些测试：

```ini
[TouchPad]
Enabled=1

[Mouse]
Enabled=0
```

- `[TouchPad] Enabled` 缺失时默认 `1`；`[Mouse] Enabled` 缺失时默认 `0`。至少保留一个 `Enabled=1`。
- 只测普通鼠标：`[TouchPad] Enabled=0`，`[Mouse] Enabled=1`。
- 两项都启用时依次运行 TouchPad、Mouse；只有两项都 PASS，整体才 PASS。任一项 FAIL/CANCEL 不会跳过后续已启用测试，最终统一汇总。
- 被禁用测试的配置节不会被解析，其错误不会阻止另一项测试。

窗口覆盖主显示器完整屏幕。TouchPad 测试隐藏鼠标指针；Mouse 测试显示鼠标指针。两项测试都没有整体超时，未完成时持续等待输入；`Esc` 或关闭窗口为 FAIL。

## TouchPad 测试

1. 用触摸板划过中心网格，首次覆盖的格子变绿。
2. 完成配置要求的左键、右键次数；点击窗口任意位置均计入，不鉴别点击来源。
3. 全部必测格、左键和右键次数完成后自动 PASS。

设备识别固定使用原始输入 HID 顶层集合 `UsagePage=0x0D`、`Usage=0x05`。找不到 Precision Touchpad 或注册 Raw Input 失败时 FAIL，日志记录具体 API/Win32 错误。

## Mouse 测试

设备识别使用原始输入 `RIM_TYPEMOUSE`，不按设备名称、索引或 PnP 描述筛选；日志记录实际发现的设备路径和按键数。找不到普通鼠标或注册 Raw Input 失败时 FAIL。

以下 7 项全部完成后自动 PASS，结果页停留约 1 秒后关闭并退出：

| 项目 | 判定 |
| --- | --- |
| 滑动鼠标 | 水平、垂直各达到 `MoveAxisDistance`，总计达到 `MoveDistance`；单位为鼠标原始移动计数，与屏幕 DPI 无关 |
| 滚轮滚动 | 向上、向下各达到 `WheelSteps` 格；一格按 120 原始滚轮单位累计 |
| 左键单击 | 左键按下并松开，按下期间移动不超过 `ClickTolerance` |
| 左键双击 | 两次左键单击间隔不超过系统双击时间，且两次按下期间移动都不超过 `ClickTolerance` |
| 右键单击 | 右键按下并松开 |
| 滚轮点击 | 中键（滚轮）按下并松开 |
| 按住左键拖动 | 左键按下后移动达到 `DragDistance`，再松开 |

- 按下期间移动超过 `ClickTolerance` 但未达到 `DragDistance` 的一次操作，既不算单击也不算拖动，需要重做。
- 拖动完成也计入鼠标滑动距离；点击和拖动可以按任意顺序完成。
- 双击计数前的一次单击会正常计入“左键单击”，因此先双击也可以同时完成单击项。
- 窗口失去焦点时清除未释放的按键状态，避免一直保持“按下”。

### 界面和笔迹

- 鼠标测试是全屏笔迹画布：笔迹跟随系统鼠标的位置，不再限制在右侧小面板。
- **蓝色细线**：普通移动。
- **橙色粗线**：按住左键移动（拖动路径）。
- **绿色圆点**：左键单击（含双击的每次单击）。
- 7 个测试项压缩到界面顶部，用 7 个小圆点 + 小标签显示；已完成绿色带勾，有进度但未完成蓝色，未开始灰色。
- “滑动”小圆点下方常驻一行实时滑动量：`总 x / 目标`、蓝色进度条和 `X x/目标 · Y y/目标` 分轴计数；移动鼠标时数字实时变化，完成后变绿。
- 顶部只占一条窄栏；其余屏幕都可以绘制笔迹。底部保留蓝色/橙色/绿色图例。
- 界面文字支持中文、英文、越南文。

## 配置

程序目录下的 `config.ini` 为 UTF-8 明文；文件不存在时使用内置默认值：TouchPad 启用、Mouse 关闭，TouchPad 5 行 × 10 列、左右键各 1 次，Mouse 全部 7 项按要求次数。

```ini
[language]
Culture=zh-CN

[TouchPad]
Enabled=1
RowCount=5
ColumnCount=10
LeftClickCount=1
RightClickCount=1
Finger=

[Mouse]
Enabled=0
MoveDistance=1200
MoveAxisDistance=300
WheelSteps=3
LeftClicks=1
DoubleClicks=1
RightClicks=1
MiddleClicks=1
Drags=1
DragDistance=300
ClickTolerance=8
```

- `Culture` 支持 `zh-CN`、`en-US`、`vi-VN`，不区分大小写。缺失时默认中文；非法值使启动 FAIL。
- `Enabled` 只允许 `0` 或 `1`。
- TouchPad 的 `RowCount` / `ColumnCount` 成对回退；`LeftClickCount` / `RightClickCount` 成对回退；`Finger` 规则保持原模块行为。
- Mouse 的已知键必须是规定范围内的非负整数；非法值、`MoveDistance < MoveAxisDistance`、或启用拖动时 `DragDistance <= ClickTolerance` 都会使启动 FAIL。
- Mouse 单项要求设为 `0` 可关闭该项，例如 `MiddleClicks=0`、`WheelSteps=0`、`Drags=0`。
- 节名、键名不区分大小写；未知键忽略。

配置错误只阻止本次测试启动，程序会显示错误并写日志，不会创建测试窗口。

## 日志和退出码

`MouseTest.log` 写在程序目录，UTF-8 追加。开始、选中测试、设备、错误、每项结果、结束时间、耗时和最终结果先在内存缓存，测试结束后一次写入；日志保存失败会弹窗并返回 3。启用的测试会各自写入 `===== TouchPad =====` / `===== Mouse =====` 段。

| 退出码 | 含义 |
| --- | --- |
| 0 | 全部已启用测试 PASS，日志已保存 |
| 1 | 任一测试 FAIL，包括 Esc、关窗、配置错误、设备未找到、窗口异常 |
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

TouchPad 和 Mouse 的配置解析、判定逻辑、隐藏窗口生命周期、PASS、Esc、关窗及按钮/滚轮/拖动注入已有定向测试；正式 EXE 编译和版本信息已核对。

2026-10-17 已在开发机（DESKTOP-H71214H，Windows 11 10.0.26300，2560×1440）用实际鼠标完成鼠标测试全部 7 项，日志记录 `Mouse result: PASS`、耗时 27.343 秒、退出码 0。当时在线设备包括 Razer Viper V3 HyperSpeed（`VID_1532&PID_00B8`）和另一个 HID mouse（`VID_391D&PID_2A04`），测试不区分具体鼠标。

该通过来自 1.1.10.17 的判定版本；1.2/1.3 只修改鼠标界面和笔迹绘制，判定、配置、日志和清理未改。1.2 的笔迹颜色/路径、1.3 的实时滑动量行均已用真实窗口视觉夹具观察，但尚未重新完成一轮实际 7 项操作，仍需现场复核。

该通过仅代表这一台开发机、这一组鼠标和当时的配置，不代表目标工厂机、其他鼠标型号或全部 DPI。TouchPad 真实硬件仍未验证（当前开发机没有 Precision Touchpad），状态为**代码完成未实测**。鼠标测试状态为**开发机实机通过，目标机待验证**。

行为细节见 `docs\BEHAVIOR_BASELINE.md`，验证范围见 `docs\releases\v1.3.10.17.md`。
