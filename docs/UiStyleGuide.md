# 界面样式说明

需求 §九 的三条交付说明都在这一份里：每个窗口的样式与状态、深浅主题的对照、高 DPI 与多屏适配。
文中每一条都能在代码或用例里找到落点；写「由用例钉住」的地方指的是本仓的 ctest 用例，不是计划要加的。

## 1. 令牌放在哪儿

| 类别 | 真源 | 说明 |
| --- | --- | --- |
| 语义色（三套主题各一份） | `src/Ui/DesignTokens.h` 的 `ThemePalette`、`kLightPalette`、`kDarkPalette`、`kHighContrastPalette` | 绘制代码只认角色名（`windowBackground`、`surface`、`text`、`secondaryText`、`border`、`accent`、`onAccent`、`selection`、`icon`、`success`、`danger`），不认十六进制 |
| 跨模块共用的绘制常量 | 同上：`kCaptureMaskColor`、`kAccentColor`、`kCaptureHintTextColor`、`kCaptureHintBackgroundColor`、`kCrosshairColor`、`kHintFontSize`、`kHintPadding`、`kToolbarPadding`、`kToolbarSliderWidth`、`kSelectionBorderWidth`、光标高亮一族 | 遮罩、选区读数、工具栏、录屏高亮这些「两个模块都要用到」的尺寸与色 |
| 字号与线宽的阶梯 | 同上：`kFontSizes`（12/13/14/16/20/24）、`kAnnotationWidths`（2/4/6/8）、默认档位下标 `kDefaultAnnotationWidthStep`/`kDefaultAnnotationFontSizeIndex`、找回函数 `stepIndexOf` | 界面只允许走阶梯；配置里存了阶梯外的值时 `stepIndexOf` 返回空，调用方记日志后回落默认档，不就近取一档 |
| 标注色板 | 同上：`kAnnotationPalette` 八色与 `annotationColorAt` | 序号越界返回空色而不是夹到最后一名，少画一个色块要能被发现 |
| 模块自身的几何常量 | 与消费者同目录：`History/HistoryThumbnail.h` 的 `kThumbnailCornerRadius`、`Pick/ColorPickerOverlay.cpp` 的 `kMagnifier*`/`kInfoStripHeight`、`Record/RecordingHud.cpp` 的 `kHudEdgeGap`/`kCountdownFontPointSize`、`Ocr/OcrResultWindow.cpp` 的 `kPreviewMinimum*` | 只有一个消费者的尺寸不上收到全局，避免造没人吃的键 |
| 控件外观（内边距、圆角、边框宽度） | `Resources/Styles/Light.qss`、`Dark.qss`、`HighContrast.qss` | Qt 样式表是这些属性的唯一出处；三份各写一遍是因为它们的色值本来就不同，圆角与间距在三份里保持同值 |
| 动效 | 刻意没有 | 需求 §九 的禁止项：动画不得干扰截图、标注与录屏。全仓没有 `QPropertyAnimation`/`transition`；Toast 的停留时间是固定毫秒数，不是补间动画 |

样式表的挂载点在 `src/Ui/ThemeManager.cpp`：解算出档位后 `application->setStyleSheet(styleSheetFor(resolved))`，
`styleSheetFor` 读 `:/styles/<档>.qss` 资源；资源缺失时返回空串并记一条含资源路径的错误日志，
不装成「已应用」（这条由 `ThemeManagerFixture.MissingStyleSheetYieldsEmptyTextNotAFakeTheme` 钉）。

## 2. 每个窗口的样式与状态

状态命名统一按 Qt 样式表的伪状态：`:hover`、`:pressed`、`:focus`、`:disabled`、`:checked`，
子控件用 `::indicator`、`::handle`、`::drop-down`。三份 QSS 都覆盖到同一批选择器
（`QPushButton`、`QToolButton`、`QLineEdit`/`QComboBox`/`QSpinBox`、`QCheckBox`/`QRadioButton`、
`QGroupBox`、`QDialog`、`QWidget` 基础色），差异只在色值。

| 窗口 | 类与文件 | 样式来源 | 状态 |
| --- | --- | --- | --- |
| 主窗口（设置 / 历史 / 关于入口） | `src/App/MainWindow.cpp` | 全部走 QSS 与布局：左侧导航 `QListWidget`（宽 `kNavColumnWidth`=150）+ `QStackedWidget` 十页表单 | 导航项选中由 `QListWidget` 的 selection 色体现；控件禁用态走 `:disabled`。每页顶部一条说明行 |
| 标注窗 | `src/Annotation/AnnotationWindow.cpp` | 画布 `AnnotationView` 自绘底图与图形；工具栏另计 | 快捷键动作表（复制/保存/贴图/识别/撤销重做），不可撤销时按钮 `:disabled` |
| 标注工具栏 | `src/Annotation/AnnotationToolbar.cpp` | `kToolbarPadding` 内边距、`kToolbarSliderWidth` 固定滑块宽、`kAnnotationPalette` 八色块 + 自定义色、`RecentColors.h` 的 `kRecentColorCapacity`(6) 最近用色 | 当前工具按钮 `:checked`（三态都在 QSS 里），线宽/字号只走阶梯；色块选中用主色描边 |
| 截图遮罩 | `src/Capture/CaptureOverlay.cpp` | 自绘：`kCaptureMaskColor` 压暗背景、`kSelectionBorderWidth`+`kAccentColor` 选区框、`kCrosshairColor` 十字虚线、读数用 `kHintFontSize`/`kHintPadding` 压在 `kCaptureHintBackgroundColor` 上 | 拖拽中显示尺寸与坐标读数；`Esc` 取消、`Enter` 或双击确认（`keyPressEvent`/`mouseDoubleClickEvent`）|
| 贴图窗口 | `src/Pin/PinWindow.cpp` | 无边框、置顶；只有悬停或正在拖手柄时才画主色边框（`kBorderWidth`），平时一张干净的图不带装饰 | 缩放（滚轮）、旋转、透明度、鼠标穿透、右键菜单；`Qt::Tool \| FramelessWindowHint \| WindowStaysOnTopHint`；穿透态下不抢焦点 |
| 识别结果窗 | `src/Ocr/OcrResultWindow.cpp` | QSS 为主；预览区最小尺寸 `kPreviewMinimumWidth`/`kPreviewMinimumHeight` | 左侧原图右侧文本，点一行在图上圈出该行；置信度那一格在后端给不出时显示「未提供」（不是 0%） |
| 录屏控制条 | `src/Record/RecordingHud.cpp` | 悬浮小窗：`kHudEdgeGap` 边距、`kCountdownFontPointSize` 倒计时字号 | 倒计时→录制中→暂停→停止；按钮可用态随服务状态切换 |
| 取色器 | `src/Pick/ColorPickerOverlay.cpp` | 自绘放大镜：`kMagnifierSampleEdge`/`kMagnifierViewEdge` 取样与显示格数、`kMagnifierPenWidth` 边框、`kInfoStripHeight` 信息条 | 逐像素网格 + 十字线；颜色格式按 `Pick` 的词表切换；取色时按物理像素取样再折算 |
| Toast 通知 | `src/Ui/Toast.cpp` | 用 `palette.success`/`palette.danger`/`palette.secondaryText` 现算内联样式（这几支色不在 QSS 里，用例按消费方名单核对） | 三类：Information / Success / Danger；固定毫秒后自收 |
| 快捷键输入框 | `src/Ui/HotkeyEdit.cpp` | QSS 控件外观 | 闲置态显示已存的组合，悬浮说明写清「点击这一格后按下组合键即可替换，按 Esc 取消」；进入录制态换成「请按下快捷键（Esc 取消）」的提示 |
| 历史列表 | `src/History/HistoryView.cpp` | 缩略图边长 `kThumbnailEdge`、圆角 `kThumbnailCornerRadius` | 按类型筛选、顺序、保留策略读数；缩略图读不出来时给一枚类别图标（`Record`/`CaptureRegion`），不留空白方块 |

## 3. 深浅主题对照

三档：浅色、深色、高对比度（`Ui/ThemeKind` 另有 System，解算成前两者之一，解算失败兜底到浅色）。
逐角色对照（十六进制以 `DesignTokens.h` 为准）：

| 角色 | 浅色 | 深色 | 高对比度 | 用途 |
| --- | --- | --- | --- | --- |
| windowBackground | `#F3F4F6` | `#1B1D22` | 纯白 | 窗口底 |
| surface | 纯白 | `#24262C` | 纯白 | 面板、按钮、列表项 |
| text | `#1B1B1F` | `#E9EBEE` | 纯黑 | 正文 |
| secondaryText | `#4A5058` | `#B4B9C0` | 纯黑（刻意不比正文弱） | 提示与读数 |
| border | `#C9CDD3` | `#3C4048` | 纯黑 + 2px（这一档反过来要求用纯黑） | 1px 边框 |
| accent | `#007ACC` | 与浅色同一支 | `#0B57D0` | 焦点、主按钮、非文本对比 |
| selection | `#BFDDF5` | `#1E4A68` | 纯黄（让纯黑正文压上去仍是最强对比） | 文本选区 |
| icon | `#3A3F45` | `#D4D7DB` | 纯黑 | SVG `currentColor` 的替换色 |
| success / danger | `#157A42` / `#B3261E` | `#57C782` / `#F2555A` | `#006400` / `#B00000` | Toast 与状态色 |

对比要求由用例算：`ThemeManagerFixture` 里的 `contrastRatio` 逐角色核 WCAG——正文与次要文本压在本档
底色上要 ≥ 4.5（AA），焦点圈/主按钮描边与图标色 ≥ 3.0（非文本对比），选区上的正文与主按钮上的文字
≥ 4.5；高对比度档另要求正文与次要文本都 ≥ 7.0（AAA 级）。

「截图对比」这一条给的是可复现的做法与一条渲染判据，而不是静态图片：

- 人工核：设置 → 主题 三档各切一次（切换即时生效），主窗口、标注工具栏、截图遮罩、Toast 四处在
  每档下看一遍；色值差异以上表为准。
- 机器判据：`ThemeManagerFixture.WidgetsRenderThePaletteSurfacePerTheme` 把同一件空按钮分别按
  两份样式表渲染成位图，取中部像素，断言浅色画出 `kLightPalette.surface`、深色画出
  `kDarkPalette.surface`，且两档像素不同。样式表没挂上时（实测把渲染改成空样式表）三条断言同时红。
  这条替代「肉眼看截图」的作用在于：文本比对全绿而界面还是原生灰这种形状，只有画出来才看得见。

## 4. 高 DPI 与多屏适配

- 抓帧的形状：物理像素住在 `QImage` 里，倍比住在 `Core::CapturedImage.devicePixelRatio` 与
  `logicalSize` 里；`src/Capture/QScreenCaptureSource.cpp` 刻意把 `QImage` 自己的倍比标成 1.0，
  避免同一事实有两处读数。
- 逻辑→物理的换算是唯一一处：`ScreenUtils::toPhysicalRect` 左上角向下取整、右下角向上取整，
  保证裁出的框不丢边；倍比 ≤ 0 直接拒绝并给出「dpr 不该由调用方自己猜」的中文原因。
- 标注倍比现算：`AnnotationEngine` 用「物理宽 ÷ 逻辑宽」得到 `pixelScale`，不读屏幕的
  `devicePixelRatio`——用户缩放窗口后两者才是同一条账。屏显与导出共用这套算法，
  否则马赛克会显示在一个位置、保存在另一个位置。
- 导出带倍比：`exportImage()` 在 `pixelScale != 1.0` 时把倍比标回图上，贴图据此反推逻辑尺寸
  （`PinWindow::setImage`）。这一环丢了就会把 150% 屏上截的图开成放大 1.5 倍的窗口，
  判据在跨模块集成入口 `tests/App/TestModuleFlow.cpp` 里。
- 贴图坐标：窗口位置是全局逻辑坐标（截哪儿贴哪儿，回到上一次选区的左上角），尺寸由
  `windowSizeFor(逻辑尺寸, 缩放)` 得出；旋转后的外接框另算（`PinTransform`）。
- 多显示器：抓帧与取色都按屏幕名走（空屏幕名表示「光标所在那块」），屏幕消失或几何无效时返回
  `ScreenUnavailable` 并点名是哪块屏，不悄悄退回主屏。
- 录屏光标高亮：半径 `kCursorHighlightRadius` 按逻辑像素给，乘缩放因子得到物理半径，
  200% 屏上圈的视觉大小与 100% 屏一致；主圈 `kCursorHighlightColor` 用主色，衬圈
  `kCursorHighlightHaloColor` 半透明白，压在深色画面上才看得出。
- 界面缩放不在代码里覆盖：本仓不调 `QApplication::setAttribute` 一类的高 DPI 开关，Qt 6 默认按
  系统缩放走，每块屏按自己的 `devicePixelRatio` 报几何。没有 X 的容器里读数环境是 offscreen
  平台（`scripts/container/env.sh`）。

## 5. 键盘与可访问性

- 每个动作都有键盘路径：全局热键见 README 的热键表，界面内 `Enter`/`Esc`/`Tab` 顺序由 QSS 的
  `:focus` 焦点圈（主色描边）体现，`QPushButton:default` 给主操作单独配色。
- 焦点可见性是硬要求：`:focus` 规则在三份 QSS 里都在；高对比度档把边框加粗到 2px，
  这条由 `ThemeManagerFixture` 在样式表文本里核（`2PX SOLID`）。
- 图标全部原创 SVG（24 视框、`currentColor`、1.5/2 描边），描边色由 `IconProvider` 按主题注入；
  用例 `IconProviderFixture` 数的是真的画出来的像素，不是文件名。
