# 界面样式说明

需求 §九 的三条交付说明都在这一份里：每个窗口的样式与状态、深浅主题的对照、高 DPI 与多屏适配。
浅色与深色两档是微明度分层：窗口底、卡片、控件三档中性色之间只差一点明度，控件不再靠描边分边界；
选中与勾选改走填充，压在其它内容上的浮层保留一条 1px 微边缘，焦点在输入面上画双环。
高对比度档保持自己的无障碍契约：纯黑纯白、2px 描边、纯黄选区。
文中每一条都能在代码或用例里找到落点；写「由用例钉住」的地方指的是本仓的 ctest 用例。

## 1. 令牌放在哪儿

| 类别 | 真源 | 说明 |
| --- | --- | --- |
| 语义色（三套主题各一份） | `src/Gui/Theme/DesignTokens.h` 的 `ThemePalette`、`kLightPalette`、`kDarkPalette`、`kHighContrastPalette` | 绘制代码只认角色名（`windowBackground`、`surface`、`controlFill`、`text`、`secondaryText`、`border`、`accent`、`onAccent`、`selection`、`icon`、`success`、`danger`、`mutedFill`、`hover`、`pressed`、`accentHover`、`accentSoft`、`borderStrong`），不认十六进制。`controlFill` 是按钮与输入框的静息填充，`mutedFill` 服务禁用底与只读底、滑块槽与微调框上下按钮的悬停底；`border` 是分隔线，`borderStrong` 是浮层微边缘与高对比度档的描边 |
| 应用调色板的映射 | 同上：`buildApplicationPalette`，落在 `ThemeManager::refresh` | 样式表画不到原生那部分（表头、占位与禁用文字），这些只认 `QPalette`；自绘微调框的悬停底也从 `QPalette::AlternateBase` 取。两份上色路径必须同源，否则深色档会带进一批浅色原生件 |
| 排版档位 | 同上：`TextRole` 与 `kTextRolePointSize*`、`applyTextRole`、`applyTabularFigures` | 三档：页标题 15 / 正文 10 / 读数 13（点）。刻意没有「比正文更小」的一档——小字号与次要色是两个独立的弱化手段，一起用就把「灰一点」变成「看不清」；说明行只压色不减号。走点值而不是样式表的 px（px 不随系统 DPI 缩放），正文档由 `Main::Application` 设成应用字体。读数另开 tabular figures，暗色档的纯 ASCII 读数换可变字族并降 40 字重；含中文的读数不换字族也不降重 |
| 间距阶梯 | 同上：`kSpacingTiny/Small/Medium/Large`、`kSpacingHairline`、`kFieldMaximumWidth`、`kIntroMaximumWidth` | 界面里出现的每个间距都取自 2/4/8/12/16；字段与说明行的宽度上限管住「一行长句」和「整窗宽的输入框」，设置窗默认 680×560、导航 160 宽，让一行就是一条能扫读的行。设置页表单的纵向间距是 `kSpacingSmall`(8)，给 6px 外扩的焦点环留出呼吸区；只有标注工具栏继续用 `kSpacingHairline`(2) 的紧凑档 |
| 跨模块共用的绘制常量 | 同上：`kCaptureMaskColor`、`kAccentColor`、`kCaptureHintTextColor`、`kCaptureHintBackgroundColor`、`kCrosshairColor`、`kHintFontSize`、`kHintPadding`、`kToolbarSliderWidth`、`kSelectionBorderWidth` | 遮罩、选区读数、工具栏滑块这些「两个模块都要用到」的尺寸与色；录屏光标高亮一族（`kCursorHighlight*`）随绘制代码住在 `Core/Record/CursorHighlight.h`，与 `accent` 同值的关系由 `DesignTokens.CursorHighlightColorFollowsTheAccent` 钉住 |
| 字号与线宽的阶梯 | 同上：`kFontSizes`（12/13/14/16/20/24）、`kAnnotationWidths`（2/4/6/8）、默认档位下标 `kDefaultAnnotationWidthStep`/`kDefaultAnnotationFontSizeIndex`、找回函数 `stepIndexOf` | 界面只允许走阶梯；配置里存了阶梯外的值时 `stepIndexOf` 返回空，调用方记日志后回落默认档，不就近取一档 |
| 标注色板 | `Core/Annotation/AnnotationPalette.h`：`kAnnotationPaletteColors` 八色、`defaultAnnotationPalette()`、`annotationPalette(settings)` 与 `paletteToTokens`/`removePaletteColor` | 越界序号原样交回而不是夹到最后一名，少画一个色块要能被发现；蓝那格与主色同值由 `DesignTokens.AnnotationPaletteBlueFollowsTheAccent` 钉住 |
| 模块自身的几何常量 | 与消费者同目录：`Core/History/HistoryThumbnail.h` 的 `kThumbnailEdge`、`Gui/Pick/ColorPickerOverlay.cpp` 的 `kMagnifier*`/`kInfoStripHeight`、`Gui/Record/RecordingHud.cpp` 的 `kHudEdgeGap`/`kCountdownFontPointSize`、`Gui/Ocr/OcrResultWindow.cpp` 的 `kPreviewMinimum*` | 只有一个消费者的尺寸不上收到全局，避免造没人吃的键 |
| 控件外观度量（内边距、最小尺寸、外边距）与边框占位 | `src/Gui/Resources/Styles/Light.qss`、`Dark.qss`、`HighContrast.qss` | 样式表只留文字色、选区色、内边距、`min-width`/`min-height`/`margin`、分隔线与 `1px`/`2px solid transparent` 的边框占位；填充、描边与圆角由自绘代码按 §1.6 的角色画。浅色与深色两份逐条同构、只差色值与占位宽；高对比度档的可见 2px 描边也由自绘代码给出。下拉箭头是 `src/Gui/Resources/Styles/` 下的 `ComboChevron*.svg` 三份；勾的对勾已由 `Gui::CheckBox` 自绘，`CheckmarkOnAccent.svg` 只剩测试用的形状参照 |
| 动效 | `src/Gui/Theme/Motion.h/.cpp`：`kSnapSpring`(1200/80)、`kPopSpring`(380/18)、`kSpringFrameMs`、`springProgress`/`springSettleMs`、`cubicBezier`、`SpringValue`、`animationsEnabled` | 白名单，不是「全仓没有动画」：Toast 的成功/信息提示逐字显现、成功图标微弹；设置页复选框勾选微弹（取消不弹）；贴图松手吸附屏边与贴边收展走几何弹簧。截图选框、标注画布与工具栏、录屏控制条一律不加动画，贴图的拖动与改大小也不插动画——跟手优先。系统「显示动画」关闭时全部瞬时到位（见 §5） |

样式表的挂载点在 `src/Gui/Theme/ThemeManager.cpp`：解算出档位后一次落 `application->setStyleSheet(...)` 与
`application->setPalette(buildApplicationPalette(...))`，`styleSheetFor` 读 `:/styles/<档>.qss` 资源；
资源缺失时返回空串并记一条含资源路径的错误日志，不装成「已应用」（这条由 `ThemeManagerFixture.MissingStyleSheetYieldsEmptyTextNotAFakeTheme` 钉）。
同一次 `refresh` 里还会把新档位写进文字排版，并遍历 `allWidgets` 重放文字角色与等宽数字——不重放时控件会停在旧档的字体与字重上。

**样式表不铺裸 `QWidget`**：QSS 只给顶层 `#appRoot` 铺窗口底；`#pageCard` 设置页、`#hudRoot` 录屏控制条、
`#AnnotationToolbar` 标注工具栏的底色与形状由自绘类画，`#settingsScroll` 与视口保持透明。
给裸 `QWidget` 铺底等于让每个子控件各画一块自己的底，表单里会排出一条条灰带。
QWidget 子类要吃样式表的背景必须 `setAttribute(Qt::WA_StyledBackground, true)`；现在只有 `#appRoot` 走这条，
另外三个具名容器由各自的 `paintEvent` 提供可见像素。

## 1.5 视觉语言：微明度分层

浅色与深色两档共用同一套骨架，只换色值：

- 分层：窗口底 `windowBackground`、卡片 `surface`、控件 `controlFill` 三档之间只差一点明度；
  悬停 `hover` 与按下 `pressed` 再各加深一档；禁用与只读落 `mutedFill`，
  滑块槽与微调框上下按钮的悬停底也取它。
  卡片的圆角按 `Card` 档（20px、平滑度 0.80，见 §1.6）、内边距 `kSpacingMedium`，底色差已经足够分出「这是一块阅读面」。
- 无容器边框：`#pageCard`、按钮、工具按钮、输入框（含快捷键框与标注行内编辑器）、列表、文本区一律没有可见描边。
  浅色与深色给按钮与输入类保留一条 1px 透明边框占位（数字框不带占位，改为抬高内容最小高），让控件总高固定为按钮 30、输入类 32；
  换色只换底，不换尺寸。高对比度档的 2px 描边是契约，不按这一条归一化：
  按钮 32、输入类 34、数字框 38，由 `ThemeManagerFixture.ControlHeightsStayPinnedPerTheme` 按档钉住。
- 浮层微边缘：压在其它内容上的面板在自绘时保留一圈 `borderStrong` 边缘（浅深 1px、高对比度 2px），
  因为 Qt 样式表没有 box-shadow，浮层只能靠这道边缘与底下的内容分开。用得上的是 `Gui::Menu`、
  `QToolTip`、`QComboBox QAbstractItemView`、`#hudRoot`、`#AnnotationToolbar` 与 Toast 气泡。
- 选中与勾选不再描边：导航当前项、列表选中项、勾选的工具按钮走 `accentSoft` 填充；
  复选框指示器勾选时走 `accent` 填充，勾本身按 `onAccent` 自绘（勾选且禁用时改取次要文本色，见 §2）。
- 形状：圆角半径与平滑度按 `Core::CornerRole` 分档（见 §1.6），QSS 不再写 `border-radius`；
  可见形状由 `Core::continuousCornerPath` 与 `Gui` 的自绘控件绘制。标注工具栏是 26px 紧凑档（图标 20），
  节距用 `kSpacingHairline`(2)；所有工具档的宽度 ≤1536 由
  `ToolbarFixture.EveryToolStaysWithinTheCommonScreenWidth` 钉死。
- 按钮：次级按钮静息是 `controlFill` 填充，悬停 `hover`、按下 `pressed`、禁用 `mutedFill`；
  主按钮（`:default`）走 `accent` 填充，悬停走 `accentHover`，禁用也落回 `mutedFill`。
  静息按钮与卡片底必须有可测的明度差，否则「导出配置」这类次级按钮只剩一串文字
  （`ThemeManagerFixture.SecondaryButtonsStayDistinguishableAtRest`）。
- 菜单：字号 9pt（`kMenuTextPointSize`），项内边距 `4px 18px 4px 8px`，分隔线外边距 `2px 6px`；
  托盘、主窗「更多」与贴图右键菜单共用这一档。
- 焦点：输入面用双环（2px 主色内环、2px 空隙、32% 透明度的 2px 外环），按钮与复选框用单环，
  由 `Gui::FocusRingController` 的覆盖层绘制，环的轮廓按目标控件取连续曲率角色；浅色与深色的
  标注工具栏不装环，聚焦边由控件自绘 1px 主色边。高对比度档不装覆盖层，2px 主色边同样由自绘代码画（见 §5）。
- 仅有的 rgba 例外：滚动条滑块在色板里没有角色，按 rgba 字面量写在 `Gui::ScrollBar` 的 `handleColor()` 里
  （浅色档 16%/32%、深色档 14%/28% 透明度的中性色）——同解门禁只扫 `#RRGGBB`，改它要连同那段注释一起看。
  其余线（勾选框指示器的悬停边、菜单分隔符、工具栏竖线）一律取实色角色。
- 高对比度档不随微明度分层：纯黑/纯白 + 2px 描边 + 纯黄选区；标注工具栏那处把内容尺寸各让 1px，
  节距与另两档保持同宽（否则一排工具会把宽度顶破 1536）。

## 1.6 连续曲率圆角

界面与内容的圆角统一按 Figma 的 corner smoothing 构造：每个角依次是一段三次贝塞尔、一段圆弧、再一段三次贝塞尔，
直边以零曲率接入，曲率沿角部连续过渡。路径、掩膜与描边都在 `Core/Geometry/ContinuousCorner`：

- `CornerShape` 带名义半径与 0–1 平滑度；`CornerRole` 是尺寸分档（下表），`cornerShapeFor` 给档位形状，
  `cornerShapeFor(role, size)` 额外处理 `Handle` 档（取短边一半）。
- `cornerPathParameters` 按 Figma 的 `getPathParamsForCorner` 算出 `a/b/c/d/p`、夹取后的半径与圆弧角。
  默认 `preserveSmoothing = false`：半径先夹到 `budget = min(w, h) / 2`，平滑度再夹到 `budget / r - 1`，
  角部总占位 `p = min((1 + s) * r, budget)`，圆弧角为 `90° * (1 - s)`。
- `continuousCornerPath` 在矩形内生成闭合路径；`paintContinuousCorner` 的描边按笔宽一半内缩，
  1px/2px 笔画不越出矩形；`applyCornerMask` 用 `DestinationIn` 把图片四角清成透明。
- 半径到半尺寸时预算夹取会把平滑度压到 0：`Handle` 档因此保持正圆或胶囊（滑块手柄是正圆、Toast 气泡是胶囊），
  不会被强行拉成变形圆角。内容层没有具体控件角色时按 `cornerShapeForRadius` 取平滑度
  （≤6 / ≤12 / ≤20 / ≤32 / >32 分别是 0.30 / 0.50 / 0.65 / 0.75 / 0.85），截图成品、
  标注圆角矩形与文字底板走这条；历史缩略图取 `Thumbnail` 档的平滑度。

角色与使用位置：

| 角色 | 名义半径 | 平滑度 | 使用位置 |
| --- | --- | --- | --- |
| `Card` | 20 | 0.80 | 设置页阅读面（`Gui::SurfaceWidget`）、关于页品牌底板（`Gui::SurfaceLabel`） |
| `Panel` | 16 | 0.70 | 标注工具栏、录屏控制条、长截图 HUD、菜单面板、列表容器、识别结果文本区 |
| `Control` | 12 | 0.50 | 按钮、行编辑、下拉框、微调框、快捷键显示框、标注行内编辑器、下拉弹层 |
| `Compact` | 8 | 0.35 | 工具按钮、菜单项、列表项与弹层项、工具提示 |
| `Thumbnail` | 6 | 0.30 | 历史缩略图、微调框上下按钮的悬停底 |
| `Indicator` | 5 | 0.25 | 勾选框指示器 |
| `Hairline` | 4 | 0.20 | 滑块槽与已走过段、滚动条手柄 |
| `Handle` | 短边一半 | 0 | 滑块手柄、Toast 气泡 |
| `ContentTiny` | 3 | 0.20 | 标注工具栏色块、贴图取色读数框 |

自绘职责：

- `Gui::SurfaceWidget` / `SurfaceLabel` 画具名容器与品牌底板的底和边；`Gui::Button` / `ToolButton`、
  `LineEdit`、`ComboBox`、`TextEdit`、`ListWidget`、`Menu`、`Slider`、`ScrollBar`、`CheckBox`、`SpinBox`
  在各自的绘制入口里先画形状再交给基类画内容；`Gui::ListView` 与 `ItemDelegate` 负责下拉弹层的
  面板与项高亮。
- `Gui::FocusRingController` 的覆盖层按目标控件取 `Indicator`、`Compact` 或 `Control` 形状，内外环都由
  `continuousCornerPath` 生成。
- `Gui/Theme/ControlPalette` 的 `buttonColors` / `toolButtonColors` / `inputColors` 把状态映射成填充与描边；
  `Gui::themeBorderWidth` 给 1px（浅色、深色）或 2px（高对比度）；`Gui::SquircleStyle` 作为代理样式置空
  自绘菜单与提示弹层的原生面板、置空自绘输入类的原生输入面板与边框，避免方底盖住连续曲率；
  系统文本控件的标准右键菜单是原生 `QMenu`，面板与悬停高亮仍交给原生风格。

QSS 只留颜色、排版与透明边框占位：

- 三份样式表都没有 `border-radius`，也不给形状类控件写可见的 `background-color`/`border-color`；
  文字色、选区色、下拉箭头资源、内边距、`min-width`/`min-height`/`margin`/`spacing` 仍留在样式表。
- 浅色与深色的按钮、行编辑与下拉框保留 `1px solid transparent`、高对比度档保留 `2px solid transparent` 的
  边框占位，控件总高按档不变（按钮 30、输入 32、高对比度档按钮 32 / 输入 34，由
  `ThemeManagerFixture.ControlHeightsStayPinnedPerTheme` 钉住）。
- 数字框不带边框占位：Qt 6.12 的 `QStyleSheetStyle` 在关掉原生按钮后会按微调框自身的边框与内边距裁内部
  行编辑，把行编辑收成 1px 宽，数值文本还在却画不出来。它的内容最小高因此补到 28（高对比度档 34），
  叠上微调框上下各 2px 的自有框，总高仍落在 32 与 38；内部行编辑的规则只给文字色、透明底与左右 12px 缩进。
- 按钮与输入类的透明底、透明边与内边距只落在带 `squircle` 属性的自绘类上，微调框的内部行编辑只继承透明底；
  `QMessageBox`、`QInputDialog`、`QColorDialog` 里的原生按钮与 `QLineEdit`/`QComboBox`/`QSpinBox`
  保持系统外观。`QToolTip` 规则不能写任何 `border` 声明，否则样式引擎会自己画完提示面板，
  `SquircleStyle` 收不到 `PE_PanelTipLabel`。
- 菜单与下拉弹层的顶层窗关闭自动填底并开透明背景；菜单面板由 `Gui::Menu` 自绘，`SquircleStyle`
  只置空自绘菜单的原生面板，标准右键菜单（原生 `QMenu`）保留原生面板与悬停高亮。
  下拉弹层由 `Gui::ListView` 在容器底上描一圈 `borderStrong` 微边缘（浅深 1px、高对比度 2px）。
  提示弹层由 `SquircleStyle` 在 `PE_PanelTipLabel` 上重画面板；`polish` 时给提示窗打开透明底并关自动填底，
  `SH_ToolTip_Mask` 返回 0，原生方形窗口底与掩膜不会切掉连续曲率的角。
- 不变量由 `StyleSheetsHaveNoBorderRadius`、`SourceHasNoRawShapeWidgetConstruction`、
  `SourceHasNoRoundedRectDrawingOrInlineRadii`、`TooltipRuleKeepsNoBorderDeclaration`、
  `InputTransparencyStaysScopedToTheSquircleClasses`、`ButtonRulesStayScopedToTheSquircleClasses` 与
  `MenuRulesStayScopedToTheSquircleMenus` 钉住。

## 2. 每个窗口的样式与状态

状态命名仍按 Qt 的伪状态语义：`:hover`、`:pressed`、`:focus`、`:disabled`、`:checked`；
迁移后样式表不再给这些状态写形状规则，填充与描边由自绘类按同一套语义取，子控件只剩尺寸与透明边框占位。
浅色与深色两份 QSS 逐条同构，覆盖同一批选择器（`QPushButton[squircle="true"]`/`QToolButton[squircle="true"]`、
`QLineEdit[squircle="true"]`/`QComboBox[squircle="true"]`/`QSpinBox[squircle="true"]`、
`QMenu[squircle="true"]`、`QCheckBox`、`QToolTip`、`QListWidget`、`QTextEdit`，
以及具名容器 `#appRoot`/`#hudRoot`/`#AnnotationToolbar`/`#toolbarSeparator`/`#annotationTextEditor`/
`#settingsScroll`/`#settingsNav`/`#hotkeyDisplay`/`#annotationColorButton` 与文字角色
`#pageHint`/`#metricValue`），差异只在色值与占位宽。不带 `squircle` 属性的原生
`QPushButton`/`QToolButton`/`QMenu` 匹配不到按钮与菜单规则，系统对话框与标准右键菜单按原生外观绘制。滑块与滚动条完全自绘，样式表里没有它们的选择器；
`#pageCard` 也不再由样式表铺底。
下表的悬停、选中与焦点描述默认指浅色与深色两档。高对比度档的样式表覆盖同一批选择器，但只保留颜色、
2px 透明占位与文字色；选中与焦点由自绘代码用 2px 主色边表达：导航与列表的选中项、标注工具栏与工具按钮的
选中/聚焦、输入类与勾选框指示器的聚焦都是 2px 主色边，快捷键框录制态同理。这一档不装焦点环覆盖层，
滑块的焦点反转仍由 `Gui::Slider` 画（见 §5）。
`#settingsScroll` 是设置页那一层滚动区：它自己与视口都要透明，否则卡片 `#pageCard` 四周会多出一圈与主题不合的底色。
应用里没用到的控件（`QGroupBox`、`QRadioButton`、`QProgressBar`、`QTabBar`）刻意不写规则——
没人吃的样式表和没人吃的配置键一样是债。
其中 `#annotationTextEditor`（标注窗里那个多行行内输入框）单列一档而没并进 `QTextEdit` 那条：
识别结果区的规则带 8 像素内边距，而标注输入框的上下内边距必须为零——它的高度是代码按行数算的
（`AnnotationView::resizeTextEditorToContent` 只补边框与余量），有上下内边距就会把行裁掉半截。

| 窗口 | 类与文件 | 样式来源 | 状态 |
| --- | --- | --- | --- |
| 主窗口（设置 / 历史 / 关于入口） | `src/Main/MainWindow.cpp` | 根 `#appRoot` 铺窗口底（QSS）；顶部动作条 `#actionBar` 只在托盘不可用时出现——带热键提示的主操作是 `Gui::Button`（`CornerRole::Control`），其余条目收在一个 `Gui::ToolButton` 的弹出菜单 `Gui::Menu`「更多」里，两张清单都取自托盘那张表（`visibleTrayMenuEntries`）；多屏且托盘可用时逐屏截图走托盘子菜单（第一项「光标所在屏幕」，后面每块屏一条），没有托盘时同一组「整屏截图：<屏名>」落在动作条的「更多」里。下面是左侧导航 `Gui::ListWidget#settingsNav`（宽 `kNavColumnWidth`=160、不铺容器底，项高亮由 `ItemDelegate` 自绘）+ `QStackedWidget` 十页，每页是一张 `Gui::SurfaceWidget` 卡片（对象名 `#pageCard`，`CornerRole::Card`、`SurfaceFillRole::Surface`，高对比度档自动补 2px `borderStrong` 边），页内 `kSpacingMedium` 边距、表单纵向与横向间距都是 `kSpacingSmall`(8)、字段宽 ≤ `kFieldMaximumWidth` | 导航项与列表项的选中/悬停由 `ItemDelegate` 按 `CornerRole::Compact` 自绘（选中 `accentSoft`、悬停 `hover`；高对比度档选中项另加 2px 主色边）；每页顶部是 `#pageTitle`（15pt 半粗，字号由 `applyTextRole` 给）加一条 `#pageHint`（10pt 正文档、次要色，只压色不减号）；换主题时已建好的控件随 `allWidgets` 重设调色板并重放排版，自绘控件随调色板重绘 |
| 标注窗 | `src/Gui/Annotation/AnnotationWindow.cpp` | 画布 `AnnotationView` 自绘底图与图形；四周 2px 主色提示线与八枚抓点（「选择」档）只上屏、不进成品；工具栏另计 | 快捷键动作表（复制/保存/贴图/识别/撤销重做），不可撤销时按钮 `:disabled`；选择档拖边框＝重裁截图范围，途中用整屏冻帧预览并在光标处给像素放大镜与读数，工具栏与信息面板按拖动中的新范围实时跟随；图内空白拖动整体挪范围；松手从同一张冻帧裁，已有标注随裁切平移、撤销栈清空 |
| 标注工具栏 | `src/Gui/Annotation/AnnotationToolbar.cpp` | `Gui::AnnotationToolbar::paintEvent` 按 `CornerRole::Panel` 自绘卡片（`surface` 底 + `themeBorderWidth` 的 `borderStrong` 微边缘：浅深 1px、高对比度 2px；QSS 只留同宽透明占位）+ `kSpacingHairline` 间距与 26px 按钮档（`Gui::ToolButton` 按 `CornerRole::Compact` 自绘，图标 20；所有工具档宽度 ≤1536 由用例钉死），段与段之间是 `QFrame#toolbarSeparator`（`border` 分隔线）；`kToolbarSliderWidth`=56、字体下拉上限 120；颜色只占一枚按钮，弹出菜单里才是 `kAnnotationPaletteColors` 八色块、`RecentColors.h` 的 `kRecentColorCapacity`(6) 最近用色与「自定义颜色…」；最右是关闭标注的 `X`（与 `Esc` 同一条收场路） | 默认是「选择」档（`V`，不落笔；点中已有图形才选中、按住可拖动），当前工具按钮（`:checked`）走 `accentSoft` 填充（高对比度档另加 2px 主色边），线宽/字号只走阶梯；颜色按钮上的色块即当前形状的色，滚轮落在它上面才调不透明度；可见段按当前工具收放（`updateContextVisibility`）。这一档不装焦点环覆盖层（6px 外扩会压到相邻的 26px 按钮），聚焦边由 `buttonColors`/`toolButtonColors` 自绘（浅深 1px、高对比度 2px 主色），尺寸不跳 |
| 截图遮罩 | `src/Gui/Capture/CaptureOverlay.cpp` | 自绘：`kCaptureMaskColor` 压暗背景、`kSelectionBorderWidth`+`kAccentColor` 选区框、`kCrosshairColor` 十字虚线、读数用 `kHintFontSize`/`kHintPadding` 压在 `kCaptureHintBackgroundColor` 上 | 光标亮过之后放大镜一直跟着（`isMagnifierVisible()`，悬停、拖框、拉手柄三处同一判据），读数条报坐标与颜色、裸按 `C` 轮换颜色写法（只影响本次）；拖拽中另显示尺寸与坐标读数；`Esc` 取消，鼠标拖框松手即交出（键盘造的选区仍走 `Enter`/双击）。重裁范围在标注窗里做：默认「选择」档拖四边/四角，松手按新范围重抓。这一层不加任何动画 |
| 贴图窗口 | `src/Gui/Pin/PinWindow.cpp` | 无边框、置顶；悬停或正在拖手柄时画 1px 主色边框（`kBorderWidth`），被选中时画 2px（`kSelectedBorderWidth`），平时一张干净的图不带装饰 | 缩放（滚轮）、旋转、透明度、鼠标穿透、右键菜单；键盘 `R`/`Shift+R` 转正一挡、`0` 回到原始比例与角度（`resetScaleAndRotation()` 这一个出口）、左键双击关闭这一张、`Esc` 关闭；`Qt::Tool \| FramelessWindowHint \| WindowStaysOnTopHint`；穿透态下不抢焦点。松手吸附屏边、贴边收起与悬停展开走 `kSnapSpring`（过阻尼、单调逼近，约 250ms）；拖动与改大小逐帧跟手，按下即停掉进行中的弹簧，快速进出从当前几何重定向，标注期间不收起 |
| 识别结果窗 | `src/Gui/Ocr/OcrResultWindow.cpp` | 根 `#appRoot` 铺底，`kSpacingLarge` 边距 + `kSpacingMedium` 分栏间距；预览区最小尺寸 `kPreviewMinimumWidth`/`kPreviewMinimumHeight`；右侧文本区是 `Gui::TextEdit`，按 `CornerRole::Panel` 自绘底与边 | 左侧原图右侧文本，点一行在图上圈出该行；置信度那一格在后端给不出时显示「未提供」（不是 0%）；摘要读数上 tabular figures |
| 录屏控制条 | `src/Gui/Record/RecordingHud.cpp` | 无边框悬浮卡片 `#hudRoot`：面板与边缘由 `RecordingHud::paintEvent` 按 `CornerRole::Panel` 自绘（`surface` 底 + `themeBorderWidth` 的 `borderStrong`，浅深 1px、高对比度 2px；QSS 只留透明占位），按钮走 `Gui::Button` 的 `CornerRole::Control`；`kHudEdgeGap` 屏幕边距、`kSpacingSmall`/`kSpacingTiny` 内间距、`kCountdownFontPointSize` 倒计时字号 | 倒计时→录制中→暂停→停止；按钮可用态随服务状态切换；时长那一格是 `#metricValue`（13pt 半粗 + 等宽数字，秒跳不抖宽）；控制条本身不加动画 |
| 取色器 | `src/Gui/Pick/ColorPickerOverlay.cpp` | 自绘放大镜：`kMagnifierSampleEdge`/`kMagnifierViewEdge` 取样与显示格数、`kMagnifierPenWidth` 边框、`kInfoStripHeight` 信息条 | 逐像素网格 + 十字线；颜色格式按 `Pick` 的词表切换；取色时按物理像素取样再折算；HEX/RGB 读数上 tabular figures |
| Toast 通知 | `src/Gui/Controls/Toast.cpp`、`StreamingText.cpp` | 文字色与图标描边仍从色板角色现算（`palette.success`/`danger`/`secondaryText`；`success` 与 `danger` 不进 QSS，用例按消费方名单核对）；气泡由 `Toast::Bubble::paintEvent` 用 `CornerRole::Handle` 自绘胶囊（半径取短边一半、平滑度 0），填充 `surface`、描边 `borderStrong`、宽度取 `themeBorderWidth`（浅深 1px、高对比度 2px），顶层窗透明背景、四角外不铺系统底；图标与文字同一行，图标底边落在首行基线下方 0.125em | 三类：Information / Success / Danger，3 秒后自收。成功与信息提示逐字显现（第 i 字在 i×30ms 起步、单字 90ms 从模糊到清晰，缓动 `cubic-bezier(.215,.61,.355,1)`），错误提示整句即时出现；成功图标从 0.6 弹到 1.0；图标描边取该条提示的语义色；新提示顶掉旧提示时覆盖层一并销毁重启，不叠影 |
| 复选框（设置页全部勾选项） | `src/Gui/Controls/CheckBox.cpp` + QSS `QCheckBox::indicator` | 指示器由 `Gui::CheckBox::paintEvent` 按 `CornerRole::Indicator`（5px/0.25）自绘：静息 `controlFill`、悬停 `hover` + 1px `borderStrong` 边、勾选 `accent` 填充、禁用 `mutedFill`；高对比度档一律 2px 边（焦点/勾选/悬停取 `accent`，静息与禁用取 `borderStrong`）。勾由同一处自绘：启用时取 `onAccent`，勾选且禁用时取 `secondaryText`（禁用指示器底已落 `mutedFill`，白勾会看不见）；QSS 只留 18px 的 `::indicator` 尺寸与同宽透明边框占位 | 启用且勾选时从 0.55 弹性到 1.0（`kPopSpring`，约 450ms、过冲约 19.5%），取消勾选不弹；三档启用档的勾都取 `onAccent`（高对比度档是蓝底白勾，与另两档同一套取色）；勾选且禁用时勾取 `secondaryText`，不播弹簧（控件本来就没有可操作的确认）；减少动效时直接到 1.0 |
| 焦点态（所有可聚焦控件） | `src/Gui/Controls/FocusRing.cpp` | `FocusRingController` 作为应用事件过滤器，首次聚焦时创建覆盖层；宿主是能装下目标矩形外扩 6px 的最近祖先，几何按宿主坐标系算；内外两环都由 `continuousCornerPath` 生成，形状按目标控件取 `Indicator`（复选框）、`Compact`（工具按钮）或 `Control`（其余） | 输入框（`QLineEdit`、`QComboBox`、`QSpinBox`、标注行内 `QTextEdit`、`HotkeyEdit`）双环；按钮、工具按钮、复选框单环（复选框套在 indicator 上）；可编辑下拉框/微调框内部的行编辑映射回外层；只读、禁用与标注工具栏内的控件不装；滑块走手柄反转（见 §5）；高对比度档不装覆盖层，焦点边由控件自绘 2px 主色；切到高对比度档立即撤环、切回时给当前焦点补环 |
| 快捷键输入框 | `src/Gui/Controls/HotkeyEdit.cpp` | 显示框是 `Gui::SurfaceLabel`，按 `CornerRole::Control` 自绘底与边：静息 `controlFill`、录制态 `accentSoft` + 主色边（宽度取 `themeBorderWidth`）、禁用 `mutedFill`；高对比度档静息态按该档契约补 2px `borderStrong` 边，录制态的主色边是显式描边、三档都保持主色。QSS 只留文字色、内边距、最小高度与同宽透明边框占位 | 闲置态显示已存的组合，悬浮说明写清「点击这一格后按下组合键即可替换，按 Esc 取消」；进入录制态换成「请按下快捷键（Esc 取消）」的提示 |
| 数字输入框 | `src/Gui/Controls/SpinBox.cpp` | 继承 `QSpinBox`，面板按 `CornerRole::Control` 自绘；关掉原生上下按钮，右缘叠两枚自绘 chevron，悬停底取 `QPalette::AlternateBase`（即 `mutedFill`）并按 `CornerRole::Thumbnail`（6px/0.30）自绘 | 点击加/减，范围、后缀、`不限` 等语义仍由 `QSpinBox` 负责；键盘上下键照常；数值上 tabular figures |
| 历史列表 | `src/Gui/History/HistoryView.cpp` | 缩略图边长 `kThumbnailEdge`（96），圆角 `kThumbnailCornerRadius`（6）由 `Core::makeThumbnail` 按 `CornerRole::Thumbnail`（6px/0.30）掩膜；列表容器是 `Gui::ListWidget`，按 `CornerRole::Panel` 自绘底与边；列表项与导航共用 `ItemDelegate` 的 `Compact` 档高亮 | 顶部一枚搜索框（按文件名、类别或日期）加一个类别筛选下拉，次序固定时间倒序；保留策略本身在「通用」页那两格，这一页不重复摆读数。缩略图读不出来时给一枚类别图标（`Record`/`CaptureRegion`），不留空白方块；时间与尺寸读数上 tabular figures |

## 3. 深浅主题对照

三档：浅色、深色、高对比度（`Gui/Theme/ThemeKind` 另有 System，解算成前两者之一，解算失败兜底到浅色）。
浅色与深色是同一套微明度阶梯的两侧，高对比度档是本仓自己的无障碍契约档。
逐角色对照（十六进制以 `DesignTokens.h` 为准）：

| 角色 | 浅色 | 深色 | 高对比度 | 用途 |
| --- | --- | --- | --- | --- |
| windowBackground | `#F5F5F3` | `#0B0B0A` | 纯白 | 窗口底（只有具名的根铺这一档） |
| surface | `#FCFCFB` | `#151515` | 纯白 | 卡片、弹层 |
| controlFill | `#F2F2EE` | `#1A1A19` | 纯白 | 按钮、输入框、勾选框方框的静息填充 |
| hover | `#E9E9E4` | `#20201E` | `#E0E0E0` | 中性悬停底 |
| pressed | `#E3E3DE` | `#262624` | `#C8C8C8` | 中性按下底 |
| mutedFill | `#EDEDE9` | `#111110` | 纯白（这一档不靠底色分层次） | 禁用底、只读底、滑块槽底、微调框上下按钮的悬停底 |
| text | `#141412` | `#E8E8E3` | 纯黑 | 正文 |
| secondaryText | `#53534D`（对窗口底 7.09:1、对卡片底 7.54:1） | `#B0B0AA` | 纯黑（刻意不比正文弱） | 说明行、占位、禁用文字：三档都过 7:1（AAA），因为它承载的是「改了要不要重启」这类关键信息 |
| border | `#E7E7E2` | `#242422` | 纯黑 + 2px（这一档反过来要求用纯黑） | 分隔线：菜单分隔符、工具栏竖线、滑块槽描边；不做容器描边 |
| borderStrong | `#E2E2DD` | `#2A2A28` | 纯黑 + 2px | 浮层微边缘（菜单、提示、下拉弹层、控制条、标注工具栏、Toast）与高对比度档的控件描边 |
| accent | `#236DE9` | `#4C8DFF` | `#0B57D0` | 焦点环、主按钮、勾选与滑块的强调色（深色档更亮才压得住深底） |
| onAccent | 纯白 | `#0B0B0A` | 纯白 | 压在主色上的文字与自绘勾：深色档主色偏亮（压白字只有 3.2:1），配深字才够 AA |
| selection | `#D8E5F9` | `#2A3B57` | 纯黄（让纯黑正文压上去仍是最强对比） | 文本选区 |
| icon | `#1A1A19` | `#F5F5F3` | `#0B0B0A` | SVG `currentColor` 的替换色 |
| success / danger | `#3B7D21` / `#CA3932` | `#9BE35A` / `#FF7A71` | `#006400` / `#B00000` | Toast 与状态色（不进样式表） |
| accentHover / accentSoft | `#1D61D5` / `#E5EEFA` | `#3D7FF2` / `#22344D` | `#08449E` / `#D6E4FB` | 主按钮悬停 / 导航当前项与勾选的工具按钮 |

浅色与深色里不出现不透明纯黑；半透明色只出现在截图遮罩（`kCaptureMaskColor`、`kCaptureHintBackgroundColor` 与 `kCrosshairColor`，这三支是固定值、不随主题换）与滚动条滑块——滑块的 rgba 已移进 `Gui::ScrollBar`（浅色档半透明黑、深色档半透明白），样式表里不再有 rgba；`LightAndDarkSheetsUseNoOpaqueBlack` 只负责扫样式表里的不透明纯黑。
对比要求由用例算：`ThemeManagerFixture` 里的 `contrastRatio` 逐角色核 WCAG——正文压在本档
窗口底与卡片底上要 ≥ 4.5（AA），**次要文本三档都 ≥ 7.0（AAA）**（说明行与正文同字号，弱化只能靠色），
焦点环/主按钮描边与图标色 ≥ 3.0（非文本对比），选区上的正文与主按钮上的文字 ≥ 4.5；
状态色另核：次要文本压 `hover`/`pressed`/`mutedFill`/`controlFill` ≥ 4.5，正文与主色描边压 `accentSoft`
分别 ≥ 4.5 与 ≥ 3.0（选中项的底与它自己的强调色不能糊成一片），主按钮悬停时 `onAccent` ≥ 4.5；
高对比度档另加：正文对窗口底、次要文本对卡片底、选区上的正文都 ≥ 7.0（AAA 级）。

「截图对比」这一条给的是可复现的做法与一条渲染判据，而不是静态图片：

- 人工核：设置 → 主题 三档各切一次（切换即时生效），主窗口、标注工具栏、截图遮罩、Toast 四处在
  每档下看一遍；色值差异以上表为准。
- 机器判据：`ThemeManagerFixture.WidgetsRenderThePaletteSurfacePerTheme` 用 `Gui::SurfaceWidget`
  造一张 `#pageCard`，按当前档渲染成位图并取中部像素，断言浅色画出 `kLightPalette.surface`、
  深色画出 `kDarkPalette.surface`，且两档像素不同。卡片就是 `surface` 角色本身；按钮平时是
  `controlFill`，拿它取中部像素测的是另一档角色。把自绘底换回写死色或档位没对齐时两条断言会红。
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
- 焦点环覆盖层不做 DPR 换算：它是普通子控件，几何（目标矩形外扩 6px）与两环的 2px 笔宽都按逻辑像素给，
  由 Qt 按控件所在屏的 `devicePixelRatio` 折算成物理像素——自己再乘一次倍比，6px 外扩在 150% 屏上
  会变成 9 逻辑像素，收缩时与目标对不上。覆盖层也不读屏幕倍比做缓存，窗口被拖到另一块屏后
  下一次重绘就按新屏的倍比画。
- 焦点环覆盖层的坐标系：目标控件与宿主之间可能隔着零边距的行容器，覆盖层先把目标矩形
  `mapTo(宿主, ...)`，再作为宿主的子控件按宿主坐标 `setGeometry`；绘制时再把几何平移回覆盖层本地坐标。
  因此环随内容一起滚动（滚动区里宿主是内容祖先而不是视口），目标或它所在的零边距行移动、缩放时
  覆盖层跟着重定位。
- 界面缩放不在代码里覆盖：本仓不调 `QApplication::setAttribute` 一类的高 DPI 开关，Qt 6 默认按
  系统缩放走，每块屏按自己的 `devicePixelRatio` 报几何。离屏的读数环境走 Qt 的 offscreen 平台插件。
- **这一节里的倍比结论是在 dpr 1.0 那块屏上加合成倍比验出来的**：几何与像素的换算按
  `devicePixelRatio` 的分母判，用例把 1.5、2.0 这类倍比直接喂进被测函数（`windowSizeFor`、
  裁框与高亮半径那几处），所以「200% 屏上圈的视觉大小与 100% 一致」说的是算法形状。
  本机只有一块 dpr 1.0 的屏，非整数缩放（125%/150%）下系统怎么合成、字体 Hinting 与 1px 边框
  会不会落进半个像素，**没有在真面板上看过**——那需要一块真的缩放屏，靠改设置造不出这个现场。
- 排版走点值而不是样式表的 px：`TextRole` 的三档都是 `QFont::setPointSize`，Qt 按逻辑 DPI 换算，
  200% 缩放的屏上字号跟着系统一起放大。样式表里的 `font-size: Npx` 不随 DPI 缩放，
  所以这一版把颜色交给语义色板与自绘代码、把度量留在 QSS、把字号交给 `applyTextRole`（用例 `Style.SourceConventions` 不管这条，
  靠的是「QSS 里没有 font-size」这个形状——要加字号就得多写一份点值映射，别退回 px）。
- 表单行距 8px 也是为了高 DPI：6px 外扩的焦点环在 125% 屏上按逻辑像素算仍是 6px，行距 8px 时
  环落在相邻两行之间的空隙里，不会被下一个控件压住；标注工具栏那 2px 紧凑档例外，它不装覆盖层。

**换主题要当场生效，四样东西得各自跟上**：样式表整片换（Qt 会重抛光）、`QPalette` 落到应用**并且**
逐个控件重设（`QApplication::setPalette` 只规定之后新建的控件用什么，已建好的标签会停在旧档色上——
实测深色档下等于看不见字，判据 `ExistingWidgetsRepaintWithTheNewThemeText`）、文字角色与等宽数字
按新档重放（`reapplyTextRole`/`reapplyTabularFigures`）、图标要按名字重取
（`QIcon` 是取色那一刻的快照，`IconProvider::applyIcon` 把名字记在控件上，`refreshIcons` 换档时重取，
判据 `AppliedIconsReResolveAfterTheTintChanges` 与 `IconsReResolveWhenTheThemeChangesLive`）。
托盘在 `themeChanged` 上重跑一次 `install()`；焦点环控制器在同一条通知上换色，并从高对比度档切回时
给当前焦点补一个环。

## 5. 键盘与可访问性

- 每个动作都有键盘路径：全局热键见 README 的热键表，界面内 `Enter`/`Esc`/`Tab` 顺序由焦点可见性
  与控件顺序体现，`QPushButton[squircle="true"]:default` 给主操作单独配色。
- 焦点可见性是硬要求，分三档落地：
  - 浅色与深色：输入面（行编辑、下拉框、微调框、标注行内编辑器、快捷键框）用双环——贴控件边缘的
    2px 主色内环、2px 空隙、落在 4–6px 处、32% 透明度的 2px 外环；按钮、工具按钮与复选框用单环，
    复选框的环套在 indicator 上而不是整行文字。环由 `FocusRingController` 的透明覆盖层自绘，
    像素判据在 `tests/Gui/Controls/TestFocusRing.cpp`。
  - 高对比度：不装覆盖层，2px 主色焦点边由 `inputColors`、按钮族与 `CheckBox` 自绘，滑块走手柄反转
    （2px 正文色圈）；样式表只留同宽透明占位，`ThemeManagerFixture` 核 `2PX SOLID` 占位仍在，
    可见边不再由 QSS 画。
  - 标注工具栏（浅色与深色）：26px 紧凑档不装 6px 外扩的环，聚焦边由 `buttonColors`/`toolButtonColors`
    自绘（浅深 1px、高对比度 2px 主色），尺寸不跳。
- 双环不用 QSS `outline`：`outline` 只在 `PE_FrameFocusRect` 分支里落笔，`QLineEdit` 不走这条路；
  三份样式表现在都不含 `:focus` 规则（`AllSheetsDropTheControlLevelFocusOutlines` 逐档检查），
  标注工具栏与高对比度档的焦点边由自绘代码画，样式表只留 `1px`/`2px` 透明占位。
- 滑块是整件自绘：槽、已走过段与手柄都由 `Gui::Slider` 画，焦点反转（底色芯 + 正文色圈）在同一处完成，
  不依赖 QSS 的 `::handle`；三份样式表都没有滑块规则。判据是 `CheckboxAndSliderShowAKeyboardFocusRing`
  里数手柄矩形内的主色像素。
- 动效白名单与减少动效：只有 Toast 的流式文字与成功图标、设置页复选框的勾、贴图的松手吸附与
  贴边收展四处会动；截图选框、标注画布与工具栏、录屏控制条、焦点环一律不动画。
  系统「显示动画」关闭时（Windows 的 `SPI_GETCLIENTAREAANIMATION`，入口 `Platform::animationsEnabled`，
  在 `Main::Application` 启动时落一次），流式文字直接全显、弹簧直接到终值、复选框直接画到 1.0；
  错误提示本来就不流式。非 Windows 平台或查询失败按「允许动画」处理，不把取不到当成用户关掉了。
  测试通过 `setAnimationsEnabledForTesting` 注入，不依赖跑测机器的设置。
- 每个可操作控件都要写 `accessibleName`（只有按钮文本不算，图标按钮、滚动区与可聚焦
  文本一样要写）：`EveryInteractiveControlInTheMainWindowHasAnAccessibleName` 扫整棵
  控件树；Tab 序由 `TabWalkVisitsVisibleNamedControlsWithoutTraps` 逐页走——焦点每步
  都换、落点可见且有名字，每页至少四个落点再绕回起点。焦点环覆盖层自己无焦点策略、
  无无障碍名（`setAccessibleName` 为空），不吃鼠标。
- 图标全部原创 SVG（24 视框、`currentColor` 描边、2px 单档），描边色由 `IconProvider` 按主题注入；
  用例 `IconProviderFixture` 数的是真的画出来的像素，不是文件名。整套图的画法规格：
  - 网格与安全区：24 视框，内容（含 1px 描边外缘）压在 **2..22**；直线与基本形状的坐标落在整数或
    0.5 格点上（曲线路径的端点与控制点按圆角半径倒推，不受这条约束），墨迹外接框中心与几何中心相差
    不超过 2 格。判据 `BundledIconsStayInsideTheSafeZoneAtEveryDevicePixelRatio`、
    `BundledIconsSitNearTheOpticalCenter` 与 `BundledStraightGeometrySitsOnTheHalfGrid`。
  - 线宽收在 **2 一档**（不允许第二档混入）；填充点缀只允许
    `fill="currentColor" stroke="none"`，源码里不许出现写死的色值。判据
    `BundledSourcesKeepOneStrokeWidthAndThemeColorsOnly`。
  - 族内共用外框：截图族与录屏族的虚线选区、窗口外框、四角括号是同几行字面量逐字一致——
    改家族只改一处。判据 `CaptureAndRecordFamiliesShareFrameGeometry`。
  - 高 DPI 渲染必须显式给逻辑边界：`render(painter, QRectF{0,0,size,size})`。不带边界的 `render()`
    会把 viewBox 按物理像素铺满画家视口、再叠一次位图缩放比，dpr=2 实测画成两倍大且右下半被裁
    （修复同批，判据把 2×/3× 两档都钉住）。
  - 尺寸阶梯八档：16/20/24/32/48/64/96/128。16/20/24/32 是控件档位，96 是历史列表类别图标，
    48/64/128 是同族大尺寸输出；QIcon 备齐八档，并按已连接屏幕的实际倍率（含 1.25/1.5/1.75）出副本。
    判据 `EveryBundledIconPaintsAtEveryLadderSize`、`FractionalScreenRatiosGetExactPixmaps`、
    `HistoryLadderIncludesNinetySix`。
  - 品牌图在 `src/Gui/Resources/Brand/`：`#151515` 砖、`#F5F5F3` C 框、`#5BE37A` 录点；
    应用图标八档原生 PNG，`.ico` 七档（512 单独 PNG）；品牌图走固定色入口，
    不参与 `currentColor` 替换。关于页的 64px 品牌图垫在窗口底色的圆角底板上：
    Dark 档卡片与砖底同为 `#151515`，没有这层垫底砖形会整片消失。
    判据 `BrandIconAssetTest`、`BrandPixmapKeepsNativeSizesAndIgnoresTheTint`、
    `BrandIconExposesTheEightNativeSizes`、`BrandIconAndNavMetricsAreWired`、
    `AboutBrandTileKeepsItsLayerOnTheDarkCard`、`CategoryFallbackIconCarriesNinetySix`。
  - `App`/`AppIcon` 不再属于界面图标集；`Tray` 保留给录制 HUD 的“最小化到托盘”。
