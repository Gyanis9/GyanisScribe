# 界面样式说明

需求 §九 的三条交付说明都在这一份里：每个窗口的样式与状态、深浅主题的对照、高 DPI 与多屏适配。
浅色与深色两档的色板与材质自 2026-10-15 起为珍珠玻璃（scaffold-vue）的移植，裁决见 §1.5 与
换肤计划（仓外 `GyanisScribe-Reports/UiRestylePlan.md`）。
文中每一条都能在代码或用例里找到落点；写「由用例钉住」的地方指的是本仓的 ctest 用例，不是计划要加的。

## 1. 令牌放在哪儿

| 类别 | 真源 | 说明 |
| --- | --- | --- |
| 语义色（三套主题各一份） | `src/Gui/Theme/DesignTokens.h` 的 `ThemePalette`、`kLightPalette`、`kDarkPalette`、`kHighContrastPalette` | 绘制代码只认角色名（`windowBackground`、`surface`、`text`、`secondaryText`、`border`、`accent`、`onAccent`、`selection`、`icon`、`success`、`danger`、`mutedFill`、`hover`、`pressed`、`accentHover`、`accentSoft`、`borderStrong`），不认十六进制 |
| 应用调色板的映射 | 同上：`buildApplicationPalette`，落在 `ThemeManager::refresh` | 样式表画不到原生那部分（微调框箭头、表头、占位与禁用文字），这些只认 `QPalette`；两份上色路径必须同源，否则深色档会带进一批浅色原生件 |
| 排版档位 | 同上：`TextRole` 与 `kTextRolePointSize*`、`applyTextRole` | 三档：页标题 15 / 正文 10 / 读数 13（点）。刻意没有「比正文更小」的一档——小字号与次要色是两个独立的弱化手段，一起用就把「灰一点」变成「看不清」；说明行只压色不减号。走点值而不是样式表的 px（px 不随系统 DPI 缩放），正文档由 `Main::Application` 设成应用字体 |
| 间距阶梯 | 同上：`kSpacingTiny/Small/Medium/Large`、`kFieldMaximumWidth`、`kIntroMaximumWidth` | 界面里出现的每个间距都取自 4/8/12/16；字段与说明行的宽度上限管住「一行长句」和「整窗宽的输入框」，设置窗默认 680×560、导航 180 宽，让一行就是一条能扫读的行 |
| 跨模块共用的绘制常量 | 同上：`kCaptureMaskColor`、`kAccentColor`、`kCaptureHintTextColor`、`kCaptureHintBackgroundColor`、`kCrosshairColor`、`kHintFontSize`、`kHintPadding`、`kToolbarSliderWidth`、`kSelectionBorderWidth` | 遮罩、选区读数、工具栏滑块这些「两个模块都要用到」的尺寸与色；录屏光标高亮一族（`kCursorHighlight*`）随绘制代码住在 `Core/Record/CursorHighlight.h`，与 `accent` 同值的关系由 `DesignTokens.CursorHighlightColorFollowsTheAccent` 钉住 |
| 字号与线宽的阶梯 | 同上：`kFontSizes`（12/13/14/16/20/24）、`kAnnotationWidths`（2/4/6/8）、默认档位下标 `kDefaultAnnotationWidthStep`/`kDefaultAnnotationFontSizeIndex`、找回函数 `stepIndexOf` | 界面只允许走阶梯；配置里存了阶梯外的值时 `stepIndexOf` 返回空，调用方记日志后回落默认档，不就近取一档 |
| 标注色板 | `Core/Annotation/AnnotationPalette.h`：`kAnnotationPaletteColors` 八色、`defaultAnnotationPalette()`、`annotationPalette(settings)` 与 `paletteToTokens`/`removePaletteColor` | 越界序号原样交回而不是夹到最后一名，少画一个色块要能被发现；蓝那格与主色同值由 `DesignTokens.AnnotationPaletteBlueFollowsTheAccent` 钉住 |
| 模块自身的几何常量 | 与消费者同目录：`Core/History/HistoryThumbnail.h` 的 `kThumbnailEdge`、`Gui/Pick/ColorPickerOverlay.cpp` 的 `kMagnifier*`/`kInfoStripHeight`、`Gui/Record/RecordingHud.cpp` 的 `kHudEdgeGap`/`kCountdownFontPointSize`、`Gui/Ocr/OcrResultWindow.cpp` 的 `kPreviewMinimum*` | 只有一个消费者的尺寸不上收到全局，避免造没人吃的键 |
| 控件外观（内边距、圆角、边框宽度、状态色） | `src/Gui/Resources/Styles/Light.qss`、`Dark.qss`、`HighContrast.qss` | 样式表是这些属性的唯一出处；三份逐条同构、只差色值（高对比度档另把 1px 实色描边升到 2px），勾的对勾是 `src/Gui/Resources/Styles/CheckmarkOnAccent*.svg` 两份 |
| 动效 | 刻意没有 | 需求 §九 的禁止项：动画不得干扰截图、标注与录屏。全仓没有 `QPropertyAnimation`/`transition`；Toast 的停留时间是固定毫秒数，不是补间动画 |

样式表的挂载点在 `src/Gui/Theme/ThemeManager.cpp`：解算出档位后一次落 `application->setStyleSheet(...)` 与
`application->setPalette(buildApplicationPalette(...))`，`styleSheetFor` 读 `:/styles/<档>.qss` 资源；
资源缺失时返回空串并记一条含资源路径的错误日志，不装成「已应用」（这条由 `ThemeManagerFixture.MissingStyleSheetYieldsEmptyTextNotAFakeTheme` 钉）。

**样式表不铺裸 `QWidget`**：底色只给具名的容器（`#appRoot` 顶层窗口、`#pageCard` 设置页、
`#hudRoot` 录屏控制条、`#AnnotationToolbar` 标注工具栏）。给裸 `QWidget` 铺底等于让每个子控件
各画一块自己的底，表单里会排出一条条灰带——这是这一版重做之前界面最难看的那一处。
QWidget 子类要吃样式表的背景必须 `setAttribute(Qt::WA_StyledBackground, true)`，
上面这四个具名容器都是子类，所以各自带这一行。

## 1.5 视觉语言：珍珠玻璃

浅色与深色两档的色板、材质与形状移植自 scaffold-vue 的「珍珠玻璃」设计（规格在其 `DESIGN.md`，
移植裁决与 web→Qt 换算表见换肤计划（仓外 `GyanisScribe-Reports/UiRestylePlan.md`））。要点与落点：

- 语义：蓝（`accent`）= 选择与主操作，`success` = 正常状态，`danger` = 危险；字色在原设计里分五档，
  本仓只落出 `text` / `secondaryText` / `icon` 三个角色（角色表是冻结的，十八个字段之外不加）。
- 形状：圆角 8（工具按钮）/ 12（按钮与输入）/ 16（菜单、弹层、浮条）/ 20（阅读面卡片）/ 胶囊（提示）；
  控件高 36px（内容 34 + 描边），标注工具栏保持紧凑档（26px 内容 + 节距 4，宽度 ≤1920 由用例钉死）。
- 材质分层：阅读面（`#pageCard`）、浮层（`#hudRoot` / `#AnnotationToolbar` / 菜单）、槽（滑块与禁用底）。
  **投影不可表达**：Qt 样式表没有 box-shadow，设计里的 e1/e2/e3 以「细描边 + 底色差」近似；
  玻璃 `backdrop-filter` 取设计自带的实色降级档（materials.css 的 reduced-transparency 段）。
- 按钮：次级是透明底、悬停出玻璃块（`hover` 底 + `border` 描边）；主按钮（`:default`）走 `accent`
  填充；禁用只靠色值降档（QSS 没有 opacity 属性）。焦点环以实现语言取「2px 主色描边」近似设计的
  外环（QSS 没有 outline）。
- 两处已声明的例外：勾选框描边与滚动条滑块是设计里 `shade` 派生的半透明线，色板只收实色角色，
  这两处以 `rgba()` 字面量写进样式表——同解门禁只扫 `#RRGGBB`，改它们要连同样式表头注释一起看。
- 高对比度档不随珍珠玻璃走：仍是纯黑/纯白 + 2px 描边的无障碍档；标注工具栏那处把内容尺寸各让 1px，
  节距与另两档保持同宽（否则满配色板会顶破 1920）。

## 2. 每个窗口的样式与状态

状态命名统一按 Qt 样式表的伪状态：`:hover`、`:pressed`、`:focus`、`:disabled`、`:checked`，
子控件用 `::indicator`、`::handle`。三份 QSS 逐条同构，覆盖同一批选择器
（`QPushButton`、`QToolButton`、`QLineEdit`/`QComboBox`/`QSpinBox`、`QCheckBox`、`QSlider`、`QMenu`、
`QToolTip`、`QListWidget`、`QTextEdit`、`QScrollBar`，以及具名容器 `#appRoot`/`#pageCard`/`#hudRoot`/
`#AnnotationToolbar`/`#toolbarSeparator`/`#annotationTextEditor`/`#settingsScroll` 与文字角色 `#pageTitle`/`#pageHint`/`#metricValue`），差异只在色值。
`#settingsScroll` 是设置页那一层滚动区：它自己与视口都要透明，否则卡片 `#pageCard` 四周会多出一圈与主题不合的底色。
应用里没用到的控件（`QGroupBox`、`QRadioButton`、`QProgressBar`、`QTabBar`）刻意不写规则——
没人吃的样式表和没人吃的配置键一样是债。
其中 `#annotationTextEditor`（标注窗里那个多行行内输入框）单列一档而没并进 `QTextEdit` 那条：
识别结果区的规则带 8 像素内边距，而标注输入框的上下内边距必须为零——它的高度是代码按行数算的
（`AnnotationView::resizeTextEditorToContent` 只补边框与余量），有上下内边距就会把行裁掉半截。

| 窗口 | 类与文件 | 样式来源 | 状态 |
| --- | --- | --- | --- |
| 主窗口（设置 / 历史 / 关于入口） | `src/Main/MainWindow.cpp` | 根 `#appRoot` 铺窗口底；顶部一条 `#actionBar`（不铺自己的底色，跟着 `#appRoot` 走）——带热键提示的主操作是图标按钮 `QPushButton`，其余条目收在一个 `QToolButton` 的弹出菜单「更多」里，两张清单都取自托盘那张表（`visibleTrayMenuEntries`），所以不会出现「只有菜单里有」的入口；下面是左侧导航 `QListWidget#settingsNav`（宽 `kNavColumnWidth`=180，无框）+ `QStackedWidget` 十页，每页是一张 `#pageCard` 卡片，页内 `kSpacingLarge` 边距、`kSpacingSmall` 行距、字段宽 ≤ `kFieldMaximumWidth` | 导航项选中走 `::item:selected`（`accentSoft` 底 + 主色描边），悬停走 `::item:hover`；每页顶部是 `#pageTitle`（15pt 半粗）加一条 `#pageHint`（9pt 次要色） |
| 标注窗 | `src/Gui/Annotation/AnnotationWindow.cpp` | 画布 `AnnotationView` 自绘底图与图形；四周 2px 主色提示线只上屏、不进成品；工具栏另计 | 快捷键动作表（复制/保存/贴图/识别/撤销重做），不可撤销时按钮 `:disabled` |
| 标注工具栏 | `src/Gui/Annotation/AnnotationToolbar.cpp` | `#AnnotationToolbar` 卡片 + `kSpacingTiny` 间距与 26px 按钮档（一排最多五十几件控件，宽度 ≤1920 由用例钉死），段与段之间是 `QFrame#toolbarSeparator`；`kToolbarSliderWidth` 固定滑块宽、`kAnnotationPaletteColors` 八色块 + 自定义色、`RecentColors.h` 的 `kRecentColorCapacity`(6) 最近用色 | 当前工具按钮 `:checked`（`accentSoft` 底 + 主色描边，三态都在 QSS 里），线宽/字号只走阶梯；色块选中用主色描边；可见段按当前工具收放（`updateContextVisibility`，换肤批次落地） |
| 截图遮罩 | `src/Gui/Capture/CaptureOverlay.cpp` | 自绘：`kCaptureMaskColor` 压暗背景、`kSelectionBorderWidth`+`kAccentColor` 选区框、`kCrosshairColor` 十字虚线、读数用 `kHintFontSize`/`kHintPadding` 压在 `kCaptureHintBackgroundColor` 上 | 光标亮过之后放大镜一直跟着（`isMagnifierVisible()`，悬停、拖框、拉手柄三处同一判据），读数条报坐标与颜色、裸按 `C` 轮换颜色写法（只影响本次）；拖拽中另显示尺寸与坐标读数；`Esc` 取消、`Enter` 或双击确认（`keyPressEvent`/`mouseDoubleClickEvent`）|
| 贴图窗口 | `src/Gui/Pin/PinWindow.cpp` | 无边框、置顶；只有悬停或正在拖手柄时才画主色边框（`kBorderWidth`），平时一张干净的图不带装饰 | 缩放（滚轮）、旋转、透明度、鼠标穿透、右键菜单；键盘 `R`/`Shift+R` 转正一挡、`0` 与左键双击回到原始比例与角度（两处共用 `resetScaleAndRotation()` 这一个出口）、`Esc` 关闭；`Qt::Tool \| FramelessWindowHint \| WindowStaysOnTopHint`；穿透态下不抢焦点 |
| 识别结果窗 | `src/Gui/Ocr/OcrResultWindow.cpp` | 根 `#appRoot` 铺底，`kSpacingLarge` 边距 + `kSpacingMedium` 分栏间距；预览区最小尺寸 `kPreviewMinimumWidth`/`kPreviewMinimumHeight` | 左侧原图右侧文本，点一行在图上圈出该行；置信度那一格在后端给不出时显示「未提供」（不是 0%） |
| 录屏控制条 | `src/Gui/Record/RecordingHud.cpp` | 无边框悬浮卡片 `#hudRoot`（Qt 的样式表没有投影，靠描边与底色分出来）：`kHudEdgeGap` 屏幕边距、`kSpacingSmall`/`kSpacingTiny` 内间距、`kCountdownFontPointSize` 倒计时字号 | 倒计时→录制中→暂停→停止；按钮可用态随服务状态切换；时长那一格是 `#metricValue`（13pt 半粗 + 等宽数字，秒跳不抖宽） |
| 取色器 | `src/Gui/Pick/ColorPickerOverlay.cpp` | 自绘放大镜：`kMagnifierSampleEdge`/`kMagnifierViewEdge` 取样与显示格数、`kMagnifierPenWidth` 边框、`kInfoStripHeight` 信息条 | 逐像素网格 + 十字线；颜色格式按 `Pick` 的词表切换；取色时按物理像素取样再折算 |
| Toast 通知 | `src/Gui/Controls/Toast.cpp` | 用 `palette.success`/`palette.danger`/`palette.secondaryText` 现算内联样式（这几支色不在 QSS 里，用例按消费方名单核对）；气泡走胶囊圆角（999px 由 Qt 按高度收敛） | 三类：Information / Success / Danger；固定毫秒后自收 |
| 快捷键输入框 | `src/Gui/Controls/HotkeyEdit.cpp` | QSS 控件外观 | 闲置态显示已存的组合，悬浮说明写清「点击这一格后按下组合键即可替换，按 Esc 取消」；进入录制态换成「请按下快捷键（Esc 取消）」的提示 |
| 历史列表 | `src/Gui/History/HistoryView.cpp` | 缩略图边长 `kThumbnailEdge`、圆角 `kThumbnailCornerRadius`；列表项形状与导航共用 `QListWidget::item` | 顶部一枚搜索框（按文件名、类别或日期）加一个类别筛选下拉，次序固定时间倒序；保留策略本身在「通用」页那两格，这一页不重复摆读数。缩略图读不出来时给一枚类别图标（`Record`/`CaptureRegion`），不留空白方块 |

## 3. 深浅主题对照

三档：浅色、深色、高对比度（`Gui/Theme/ThemeKind` 另有 System，解算成前两者之一，解算失败兜底到浅色）。
浅色与深色是珍珠玻璃（scaffold-vue）两档的移植，高对比度档是本仓自己的无障碍契约档（web 无对应物）。
逐角色对照（十六进制以 `DesignTokens.h` 为准；设计原值与压深过程见换肤计划附录，仓外 `GyanisScribe-Reports/UiRestylePlan.md`）：

| 角色 | 浅色 | 深色 | 高对比度 | 用途 |
| --- | --- | --- | --- | --- |
| windowBackground | `#F6F8F7` | `#121517` | 纯白 | 窗口底（只有具名的根铺这一档） |
| surface | `#FBFCFC` | `#1B1F21` | 纯白 | 卡片、按钮、输入框、弹层 |
| text | `#101418` | `#EEF2F1` | 纯黑 | 正文 |
| secondaryText | `#495358`（设计原值 `#596368` 只有 5.8:1，压深保色相） | `#B0BABD` | 纯黑（刻意不比正文弱） | 说明行、占位、禁用文字：三档都过 7:1（AAA），因为它承载的是「改了要不要重启」这类关键信息 |
| border | `#E4E7E7` | `#232527` | 纯黑 + 2px（这一档反过来要求用纯黑） | 卡片描边与分隔线 |
| borderStrong | `#D6D9DA` | `#323537` | 纯黑 + 2px | 输入类控件的默认描边，比 border 重一档 |
| accent | `#236DE9` | `#4C8DFF` | `#0B57D0` | 焦点、主按钮、选中项描边（深色档更亮才压得住深底） |
| onAccent | 纯白 | `#0B1220` | 纯白 | 压在主色上的文字：深色档主色偏亮（压白字只有 3.2:1），配深字才够 AA |
| selection | `#D8E5F9` | `#273956` | 纯黄（让纯黑正文压上去仍是最强对比） | 文本选区 |
| mutedFill | `#EEF2F0` | `#15181A` | 纯白（这一档不靠底色分层次） | 禁用底、滑块槽 |
| hover / pressed | `#F2F3F3` / `#ECEDEE` | `#262A2C` / `#2D3133` | `#E0E0E0` / `#C8C8C8` | 中性悬停与按下 |
| accentHover / accentSoft | `#1D61D5` / `#E5EEFA` | `#3D7FF2` / `#233145` | `#08449E` / `#D6E4FB` | 主按钮悬停 / 导航当前项与勾选的工具按钮 |
| icon | `#7E888C` | `#8D979A` | 纯黑 | SVG `currentColor` 的替换色 |
| success / danger | `#3B7D21` / `#CA3932`（设计原值当文字不足 4.5:1，压深保色相） | `#9BE35A` / `#FF7A71` | `#006400` / `#B00000` | Toast 与状态色（不进样式表） |

对比要求由用例算：`ThemeManagerFixture` 里的 `contrastRatio` 逐角色核 WCAG——正文压在本档
底色上要 ≥ 4.5（AA），**次要文本三档都 ≥ 7.0（AAA）**（说明行与正文同字号，弱化只能靠色），
焦点圈/主按钮描边与图标色 ≥ 3.0（非文本对比），选区上的正文与主按钮上的文字 ≥ 4.5；
状态色那几档另核：次要文本压 `hover`/`pressed`/`mutedFill` ≥ 4.5，正文与主色描边压 `accentSoft`
分别 ≥ 4.5 与 ≥ 3.0（选中项的底与它自己的描边不能糊成一片），主按钮悬停时 `onAccent` ≥ 4.5；
高对比度档另加：正文对窗口底、次要文本对 surface、选区上的正文都 ≥ 7.0（AAA 级）。

「截图对比」这一条给的是可复现的做法与一条渲染判据，而不是静态图片：

- 人工核：设置 → 主题 三档各切一次（切换即时生效），主窗口、标注工具栏、截图遮罩、Toast 四处在
  每档下看一遍；色值差异以上表为准。
- 机器判据：`ThemeManagerFixture.WidgetsRenderThePaletteSurfacePerTheme` 把同一件空按钮分别按
  两份样式表渲染成位图，取中部像素，断言浅色画出 `kLightPalette.surface`、深色画出
  `kDarkPalette.surface`，且两档像素不同。样式表没挂上时（实测把渲染改成空样式表）三条断言同时红。
  这条替代「肉眼看截图」的作用在于：文本比对全绿而界面还是原生灰这种形状，只有画出来才看得见。

## 4. 高 DPI 与多屏适配

- 抓帧的形状：物理像素住在 `QImage` 里，倍比住在 `Base::CapturedImage.devicePixelRatio` 与
  `logicalSize` 里；`src/Plugins/CaptureSources/QScreenCaptureSource.cpp` 刻意把 `QImage` 自己的倍比标成 1.0，
  避免同一事实有两处读数。
- 逻辑→物理的换算是唯一一处：`ScreenUtils::toPhysicalRect` 左上角向下取整、右下角向上取整，
  保证裁出的框不丢边；倍比 ≤ 0 直接拒绝并给出「dpr 不该由调用方自己猜」的中文原因。
- 标注倍比现算：`AnnotationEngine` 用「物理宽 ÷ 逻辑宽」得到 `pixelScale`，不读屏幕的
  `devicePixelRatio`——用户缩放窗口后两者才是同一条账。屏显与导出共用这套算法，
  否则马赛克会显示在一个位置、保存在另一个位置。
- 导出带倍比：`exportImage()` 在 `pixelScale != 1.0` 时把倍比标回图上，贴图开窗时按它反推逻辑尺寸
  （`PinWindow` 的构造里那段 `m_logicalSize`）。这一环丢了就会把 150% 屏上截的图开成放大 1.5 倍的窗口，
  判据在跨模块集成入口 `tests/Main/TestModuleFlow.cpp` 里。
- 贴图坐标：窗口位置是全局逻辑坐标（截哪儿贴哪儿，回到上一次选区的左上角），尺寸由
  `windowSizeFor(逻辑尺寸, 缩放)` 得出；旋转后的外接框另算（`PinTransform`）。
- 多显示器：抓帧与取色都按屏幕名走（空屏幕名表示「光标所在那块」），屏幕消失或几何无效时返回
  `ScreenUnavailable` 并点名是哪块屏，不悄悄退回主屏。
- 录屏光标高亮：半径 `kCursorHighlightRadius` 按逻辑像素给，乘缩放因子得到物理半径，
  200% 屏上圈的视觉大小与 100% 屏一致；主圈 `kCursorHighlightColor` 用主色，衬圈
  `kCursorHighlightHaloColor` 半透明白，压在深色画面上才看得出。
- 界面缩放不在代码里覆盖：本仓不调 `QApplication::setAttribute` 一类的高 DPI 开关，Qt 6 默认按
  系统缩放走，每块屏按自己的 `devicePixelRatio` 报几何。离屏的读数环境走 Qt 的 offscreen 平台插件。
- **这一节里的倍比结论是在 dpr 1.0 那块屏上加合成倍比验出来的**：几何与像素的换算按
  `devicePixelRatio` 的分母判，用例把 1.5、2.0 这类倍比直接喂进被测函数（`windowSizeFor`、
  裁框与高亮半径那几处），所以「200% 屏上圈的视觉大小与 100% 一致」说的是算法形状。
  本机只有一块 dpr 1.0 的屏，非整数缩放（125%/150%）下系统怎么合成、字体Hinting 与 1px 边框
  会不会落进半个像素，**没有在真面板上看过**——那需要一块真的缩放屏，靠改设置造不出这个现场。
- 排版走点值而不是样式表的 px：`TextRole` 的五档都是 `QFont::setPointSize`，Qt 按逻辑 DPI 换算，
  200% 缩放的屏上字号跟着系统一起放大。样式表里的 `font-size: Npx` 不随 DPI 缩放，
  所以这一版把颜色/形状留在 QSS、把字号交给 `applyTextRole`（用例 `Style.SourceConventions` 不管这条，
  靠的是「QSS 里没有 font-size」这个形状——要加字号就得多写一份点值映射，别退回 px）。

**换主题要当场生效，三样东西得各自跟上**：样式表整片换（Qt 会重抛光）、`QPalette` 落到应用**并且**
逐个控件重设（`QApplication::setPalette` 只规定之后新建的控件用什么，已建好的标签会停在旧档色上——
实测深色档下等于看不见字，判据 `ExistingWidgetsRepaintWithTheNewThemeText`）、图标要按名字重取
（`QIcon` 是取色那一刻的快照，`IconProvider::applyIcon` 把名字记在控件上，`refreshIcons` 换档时重取，
判据 `AppliedIconsReResolveAfterTheTintChanges` 与 `IconsReResolveWhenTheThemeChangesLive`）。
托盘在 `themeChanged` 上重跑一次 `install()`。

## 5. 键盘与可访问性

- 每个动作都有键盘路径：全局热键见 README 的热键表，界面内 `Enter`/`Esc`/`Tab` 顺序由 QSS 的
  `:focus` 焦点圈（主色描边）体现，`QPushButton:default` 给主操作单独配色。
- 焦点可见性是硬要求：`:focus` 规则在三份 QSS 里都在；高对比度档把边框加粗到 2px。
  样式表文本由 `ThemeManagerFixture` 核（`2PX SOLID`），画出来的像素由
  `CheckboxAndSliderShowAKeyboardFocusRing` 核勾选框与滑块的焦点形态。
- 每个可操作控件都要写 `accessibleName`（只有按钮文本不算，图标按钮、滚动区与可聚焦
  文本一样要写）：`EveryInteractiveControlInTheMainWindowHasAnAccessibleName` 扫整棵
  控件树；Tab 序由 `TabWalkVisitsVisibleNamedControlsWithoutTraps` 逐页走——焦点每步
  都换、落点可见且有名字，每页至少四个落点再绕回起点。
  滑块有一条实现约束：QSS 的控件级边框在 `CC_Slider` 的自绘路径里不落笔（换背景色、
  补 `WA_StyledBackground` 都不救），焦点只能挂在 `::handle:horizontal:focus` 上做反转
  （底色芯 + 正文色圈）；别把它「统一」回 `QSlider:focus` 的写法。
- 图标全部原创 SVG（24 视框、`currentColor` 描边），描边色由 `IconProvider` 按主题注入；
  用例 `IconProviderFixture` 数的是真的画出来的像素，不是文件名。整套图的画法规格（2026-10-12 全量重绘后）：
  - 网格与安全区：24 视框，内容（含 0.75 描边外缘）压在 **2..22**；直线优先落在整数或 0.5 格点上，
    墨迹外接框中心与几何中心相差不超过 2 格。判据 `BundledIconsStayInsideTheSafeZoneAtEveryDevicePixelRatio`
    与 `BundledIconsSitNearTheOpticalCenter`。
  - 线宽收在 **1.5 一档**（需求允许 1.5/2，本套统一到 1.5 并拒绝第二档混入）；填充点缀只允许
    `fill="currentColor" stroke="none"`，源码里不许出现写死的色值。判据
    `BundledSourcesKeepOneStrokeWidthAndThemeColorsOnly`。
  - 族内共用外框：截图族与录屏族的虚线选区、窗口外框、四角括号是同几行字面量逐字一致——
    改家族只改一处。判据 `CaptureAndRecordFamiliesShareFrameGeometry`。
  - 高 DPI 渲染必须显式给逻辑边界：`render(painter, QRectF{0,0,size,size})`。不带边界的 `render()`
    会把 viewBox 按物理像素铺满画家视口、再叠一次位图缩放比，dpr=2 实测画成两倍大且右下半被裁
    （修复同批，判据把 2×/3× 两档都钉住）。
