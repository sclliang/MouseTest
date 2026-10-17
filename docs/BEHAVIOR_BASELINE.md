# MouseTest 行为基线

## 来源

- TouchPad 测试于 2026-10-17 从 `D:\Code\FactoryTestCases` 2.15.10.17 的 `src/modules/touch_pad` 抽出。
- 源模块文档：`docs/modules/TOUCH_PAD.md`。
- 源窗口、配置、网格判定和 Raw Input 代码原样移植；独立工具不引用源工程的模块契约、宿主窗口或其他测试模块。
- Mouse 测试为本工具新增，使用同一独立工程、全屏 D2D 窗口和日志机制。

## 测试选择

- `[TouchPad] Enabled` 缺失默认 `1`；`[Mouse] Enabled` 缺失默认 `0`。
- `Enabled` 只允许 `0` 或 `1`；两项都禁用时启动 FAIL。
- 两项都启用时固定按 TouchPad、Mouse 顺序依次运行；每一项独立创建、显示、销毁自己的窗口。
- 任一项 FAIL 或 CANCEL 不会跳过后续已启用测试；全部运行结束后统一汇总。
- 任一项 FAIL，整体 FAIL；任一项 CANCEL 且无 FAIL，整体 CANCEL；全部 PASS 才 PASS。
- 被禁用测试的配置节不解析，其非法值不会阻止已启用测试。

## TouchPad 测试

### 设备识别

- 固定使用原始输入 HID 顶层集合 `UsagePage=0x0D`、`Usage=0x05`。
- 不按设备索引、设备名称关键字、鼠标设备数量或 PnP 描述猜测。
- 窗口完成显示、置前并取得焦点后，再枚举设备并调用 `RegisterRawInputDevices`；记录实际选中的设备路径。
- 找不到 Precision Touchpad 或注册失败时 FAIL，日志记录具体 API 和 Win32 错误。
- 单个无法解析的 HID 报告只记 WARNING 并继续等待；前 20 条和后续省略提示写入日志，不改变测试结果。

### 测试流程

1. 窗口覆盖主显示器完整 `rcMonitor`，无边框，隐藏鼠标指针。
2. 操作员用触摸板划过中心网格，首次覆盖的格子变绿。
3. 首次收到 `WM_INPUT`、首次解析出触点时各记录一条 INFO，包含 Contact Count 和逻辑 X/Y 范围。
4. 逐个报告读取 Contact Count、Contact ID、X、Y 和逻辑坐标范围，把当前报告声明的有效触点直接送入网格覆盖；不检查方向、单笔长度、连续性或手指数。
5. 左右键按窗口 `WM_LBUTTONDOWN` / `WM_RBUTTONDOWN` 计数，不鉴别点击设备来源。
6. 全部必测格、左键次数和右键次数完成后自动 PASS，立即关闭窗口并退出。
7. `Esc` 和 `WM_CLOSE` 为 FAIL；没有整体超时。
8. 窗口销毁时注销 Raw Input，释放窗口、D2D/DWrite 对象和状态。

### 配置

`[TouchPad]`：

- `Enabled`：`0/1`，缺失默认 `1`。
- `RowCount` / `ColumnCount` 成对解析；任一缺失、非正整数或总格数超过 10000 时一起回退 `5/10`，记录 WARNING。
- `LeftClickCount` / `RightClickCount` 成对解析；任一缺失或不是非负整数时一起变为 `0/0`，记录 WARNING。
- `Finger` 为空表示全部必测；否则必须为 `角编号x宽x高`，`x` 或 `X` 均可，角编号 0–3，宽高为正整数且不超出网格。非空但无效时启动 FAIL。
- 节名、键名不区分大小写；未知键忽略。

注意：配置文件存在但缺少 `[TouchPad]` 节时，沿用源模块规则，按键计数回落为 0/0，不是使用内置默认 1/1。随包 `config.ini` 已包含完整示例。

## Mouse 测试

### 设备识别

- 使用 `GetRawInputDeviceList` 枚举 `RIM_TYPEMOUSE`，记录所有候选设备路径和按键数，不按名称、索引或 PnP 描述筛选。
- 窗口显示并取得焦点后调用 `RegisterRawInputDevices` 注册 `UsagePage=0x01`、`Usage=0x02`，只处理窗口收到的鼠标 `WM_INPUT`。
- 找不到普通鼠标或注册失败时 FAIL，日志记录具体 API 和 Win32 错误。
- 绝对坐标鼠标输入（`MOUSE_MOVE_ABSOLUTE`）不计移动量，只记录一次 WARNING；普通 USB/蓝牙鼠标为相对坐标。
- 鼠标测试窗口显示鼠标指针，不拦截系统光标。

### 测试流程

窗口为全屏 D2D 列表，7 项均完成后自动 PASS，结果页停留约 1 秒后关闭并退出。`Esc`/关窗 FAIL，无整体超时。

1. **滑动鼠标**：累计水平、垂直原始移动量均达到 `MoveAxisDistance`，总计达到 `MoveDistance`。
2. **滚轮滚动**：向上、向下累计原始滚轮量各达到 `WheelSteps × 120`。
3. **左键单击**：左键按下后松开，按下期间移动不超过 `ClickTolerance`。
4. **左键双击**：两次左键单击的松开间隔不超过系统 `GetDoubleClickTime()`，两次按下期间移动均不超过 `ClickTolerance`。
5. **右键单击**：右键按下后松开。
6. **滚轮点击**：中键按下后松开。
7. **按住左键拖动**：左键按下后移动量达到 `DragDistance`，再松开。

判定细节：

- 按下期间移动超过 `ClickTolerance` 但小于 `DragDistance` 的操作既不算单击也不算拖动，需要重做。
- 双击的第一下会同时计入“左键单击”；因此先双击也能完成单击项。
- 一次拖动只计拖动，不再计单击/双击。
- 一次双击后清除双击链，第三次点击重新从单击开始。
- 鼠标原始移动计数与屏幕 DPI 无关；不做屏幕坐标换算。
- 窗口失去激活时清除未释放按键状态和双击链，避免卡在按下状态。
- 拖动、滑动、滚轮和点击可以任意顺序完成。

### 配置

`[Mouse]`：

- `Enabled`：`0/1`，缺失默认 `0`。
- `MoveDistance`：0–1000000，默认 1200。
- `MoveAxisDistance`：0–1000000，默认 300；启用滑动时不得大于 `MoveDistance`。
- `WheelSteps`：0–1000，默认 3；0 关闭滚轮项。
- `LeftClicks` / `DoubleClicks` / `RightClicks` / `MiddleClicks`：0–1000，默认各 1；0 关闭对应项。
- `Drags`：0–1000，默认 1；0 关闭拖动项。
- `DragDistance`：0–1000000，默认 300；`Drags > 0` 时必须大于 `ClickTolerance`。
- `ClickTolerance`：0–10000，默认 8。
- 已知键非数字或超范围时启动 FAIL；未知键忽略；节名、键名不区分大小写。

## 通用配置与日志

程序目录 `config.ini`，UTF-8 明文；文件不存在时 TouchPad 默认 5×10、左右各 1，Mouse 默认关闭。

`[language] Culture`：

- `zh-CN`、`en-US`、`vi-VN`，ASCII 不区分大小写。
- 缺失默认中文；非法值启动 FAIL。

日志 `MouseTest.log` 写在 EXE 目录，UTF-8 追加，结束后一次写入。每项测试写入独立段；最终结果按 PASS/FAIL/CANCEL 汇总。

## 独立工具的差异

- 不进入 `FactoryTestCases.exe`，不注册模块，不读写源工程的任何配置或日志。
- 窗口 owner 为 `nullptr`，固定使用主显示器。
- 没有宿主停止入口，因此正常操作不会产生 CANCEL；CANCEL 只在消息循环异常结束时内部使用。
- 配置和日志放在**程序 EXE 所在目录**，不是当前工作目录。
- 退出码：0 PASS、1 FAIL、2 CANCEL、3 日志保存失败或无法启动。
- TouchPad 窗口类名和窗口标题改为 `MouseTest`；界面文字、颜色、布局、网格、输入和判定保持不变。

## 与源模块相同的限制

- TouchPad 左右键计数不鉴别鼠标、触摸板或其他来源；触摸板物理按键和外部鼠标点击都会计入。
- TouchPad 不要求一笔连续划过，也不检查划动方向。
- TouchPad 触点只要求当前报告声明为有效；不使用 Tip Switch 或设备名称额外过滤。
- TouchPad 多个同时触点可累计覆盖；蓝色圆点显示最后一个触点。

## 定向验证

- TouchPad 配置：大小写、`Finger` 的 `x`/`X` 混用、成对回退、缺节回退、非法 `Finger`。
- TouchPad 判定：免测区预完成、覆盖、越界坐标、左右键和完成条件。
- TouchPad 窗口：隐藏窗口创建、原关闭区域左键仍计数且不关闭、完整覆盖 + 右键 PASS、Esc 单次 FAIL、取消单次 CANCEL、`WM_CLOSE` FAIL。
- Mouse 配置：默认值、大小写、范围、成对约束、禁用项、未知键。
- Mouse 判定：滑动、滚轮、单击、双击、右键、中键、拖动、单击/拖动分界、慢双击、失焦复位、全部禁用即完成。
- Mouse 窗口：隐藏窗口注入完整 7 项 PASS、Esc 单次 FAIL、`WM_CLOSE` FAIL。
- 测试选择：默认值、仅鼠标、两项禁用、非法 Enabled、语言、禁用节不解析。
- 以上测试不注入真实 Precision Touchpad 或真实鼠标 Raw Input，不代表硬件验收。

## 待实机验证

- 真实 Windows Precision Touchpad 的 HID 报告、触点范围、边缘覆盖和快速划动；左右物理按键和按压式点击。
- 真实 USB/蓝牙普通鼠标的滑动、滚轮方向与格数、单击/双击/右键/中键/拖动手感。
- 不同鼠标按键数、滚轮分辨率、双击速度设置和系统鼠标加速。
- 100%、125%、150%、175%、200% DPI 和跨屏场景（本工具固定主显示器）。
- 目标机型、Windows 版本和鼠标/触摸板硬件标识。
