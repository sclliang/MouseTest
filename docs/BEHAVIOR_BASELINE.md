# MouseTest 行为基线

## 来源

- 2026-10-17 从 `D:\Code\FactoryTestCases` 2.15.10.17 的 `src/modules/touch_pad` 抽出。
- 源模块文档：`docs/modules/TOUCH_PAD.md`。
- 源窗口、配置、网格判定和 Raw Input 代码原样移植；独立工具不引用源工程的模块契约、宿主窗口或其他测试模块。

## 设备识别

- 固定使用原始输入 HID 顶层集合 `UsagePage=0x0D`、`Usage=0x05`。
- 不按设备索引、设备名称关键字、鼠标设备数量或 PnP 描述猜测。
- 窗口完成显示、置前并取得焦点后，再枚举设备并调用 `RegisterRawInputDevices`；记录实际选中的设备路径。
- 找不到 Precision Touchpad 或注册失败时 FAIL，日志记录具体 API 和 Win32 错误。
- 单个无法解析的 HID 报告只记 WARNING 并继续等待；前 20 条和后续省略提示写入日志，不改变测试结果。

## 测试流程

1. 窗口覆盖主显示器完整 `rcMonitor`，无边框，隐藏鼠标指针。
2. 操作员用触摸板划过中心网格，首次覆盖的格子变绿。
3. 首次收到 `WM_INPUT`、首次解析出触点时各记录一条 INFO，包含 Contact Count 和逻辑 X/Y 范围。
4. 逐个报告读取 Contact Count、Contact ID、X、Y 和逻辑坐标范围，把当前报告声明的有效触点直接送入网格覆盖；不检查方向、单笔长度、连续性或手指数。
5. 左右键按窗口 `WM_LBUTTONDOWN` / `WM_RBUTTONDOWN` 计数，不鉴别点击设备来源。
6. 全部必测格、左键次数和右键次数完成后自动 PASS，立即关闭窗口并退出。
7. `Esc` 和 `WM_CLOSE` 为 FAIL；没有整体超时。
8. 窗口销毁时注销 Raw Input，释放窗口、D2D/DWrite 对象和状态。

## 配置

程序目录 `config.ini`，UTF-8 明文。文件不存在时使用 `Config{}` 内置默认值：5×10、左键 1、右键 1、无免测区、中文。

`[language] Culture`：

- `zh-CN`、`en-US`、`vi-VN`，ASCII 不区分大小写。
- 缺失默认中文；非法值启动 FAIL。

`[TouchPad]`：

- `RowCount` / `ColumnCount` 成对解析；任一缺失、非正整数或总格数超过 10000 时一起回退 `5/10`，记录 WARNING。
- `LeftClickCount` / `RightClickCount` 成对解析；任一缺失或不是非负整数时一起变为 `0/0`，记录 WARNING。
- `Finger` 为空表示全部必测；否则必须为 `角编号x宽x高`，`x` 或 `X` 均可，角编号 0–3，宽高为正整数且不超出网格。非空但无效时启动 FAIL。
- 节名、键名不区分大小写；未知键忽略。

注意：配置文件存在但缺少 `[TouchPad]` 节时，沿用源模块规则，按键计数回落为 0/0，不是使用内置默认 1/1。随包 `config.ini` 已包含完整示例。

## 独立工具的差异

- 不进入 `FactoryTestCases.exe`，不注册模块，不读写源工程的任何配置或日志。
- 窗口 owner 为 `nullptr`，固定使用主显示器；源模块使用 owner 所在显示器。
- 没有宿主停止入口，因此正常操作不会产生 CANCEL；CANCEL 只在消息循环异常结束时内部使用。
- 配置和日志放在**程序 EXE 所在目录**，不是当前工作目录。
- 日志在内存缓存，结束后一次写 `MouseTest.log`；不写源工程的 `LOG\result.log`。
- 退出码：0 PASS、1 FAIL、2 CANCEL、3 日志保存失败或无法启动。
- 窗口类名和窗口标题改为 `MouseTest`；界面文字、颜色、布局、网格、输入和判定保持不变。

## 与源模块相同的限制

- 左右键计数不鉴别鼠标、触摸板或其他来源；触摸板物理按键和外部鼠标点击都会计入。
- 不要求一笔连续划过，也不检查划动方向。
- 触点只要求当前报告声明为有效；不使用 Tip Switch 或设备名称额外过滤。
- 多个同时触点可累计覆盖；蓝色圆点显示最后一个触点。

## 定向验证

- 配置：大小写、`Finger` 的 `x`/`X` 混用、成对回退、缺节回退、非法 `Finger`。
- 判定：免测区预完成、覆盖、越界坐标、左右键和完成条件。
- 窗口：隐藏窗口创建、原关闭区域左键仍计数且不关闭、完整覆盖 + 右键 PASS、Esc 单次 FAIL、取消单次 CANCEL、`WM_CLOSE` FAIL。
- 以上测试不注入真实 Precision Touchpad Raw Input，不代表硬件验收。

## 待实机验证

- 真实 Windows Precision Touchpad 的 HID 报告、触点范围、边缘覆盖和快速划动。
- 左右物理按键和按压式点击。
- 100%、125%、150%、175%、200% DPI 和跨屏场景（本工具固定主显示器）。
- 目标机型、Windows 版本和触摸板硬件标识。
