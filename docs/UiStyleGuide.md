# 界面样式说明

需求 §九 的三条交付说明都在这一份里：每个窗口的样式与状态、深浅主题的对照、高 DPI 与多屏适配。
浅色与深色两档是微明度分层：窗口底、卡片、控件三档中性色之间只差一点明度，控件不再靠描边分边界；
选中与勾选改走填充，压在其它内容上的浮层保留一条 1px 微边缘，焦点在输入面上画双环。
高对比度档保持自己的无障碍契约：纯黑纯白、2px 描边、纯黄选区。
文中每一条都能在代码或用例里找到落点；写「由用例钉住」的地方指的是本仓的 ctest 用例，不是计划要加的。

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
| 控件外观（内边距、圆角、边框宽度、状态色） | `src/Gui/Resources/Styles/Light.qss`、`Dark.qss`、`HighContrast.qss` | 样式表是这些属性的唯一出处；浅色与深色两份逐条同构、只差色值，高对比度档另把描边升到 2px 并把焦点写成控件级描边。下拉箭头与勾的对勾是 `src/Gui/Resources/Styles/` 下的 `ComboChevron*.svg` 三份、`CheckmarkOnAccent*.svg` 两份 |
| 动效 | `src/Gui/Theme/Motion.h/.cpp`：`kSnapSpring`(1200/80)、`kPopSpring`(380/18)、`kSpringFrameMs`、`springProgress`/`springSettleMs`、`cubicBezier`、`SpringValue`、`animationsEnabled` | 白名单，不是「全仓没有动画」：Toast 的成功/信息提示逐字显现、成功图标微弹；设置页复选框勾选微弹（取消不弹）；贴图松手吸附屏边与贴边收展走几何弹簧。截图选框、标注画布与工具栏、录屏控制条一律不加动画，贴图的拖动与改大小也不插动画——跟手优先。系统「显示动画」关闭时全部瞬时到位（见 §5） |

样式表的挂载点在 `src/Gui/Theme/ThemeManager.cpp`：解算出档位后一次落 `application->setStyleSheet(...)` 与
`application->setPalette(buildApplicationPalette(...))`，`styleSheetFor` 读 `:/styles/<档>.qss` 资源；
资源缺失时返回空串并记一条含资源路径的错误日志，不装成「已应用」（这条由 `ThemeManagerFixture.MissingStyleSheetYieldsEmptyTextNotAFakeTheme` 钉）。
同一次 `refresh` 里还会把新档位写进文字排版，并遍历 `allWidgets` 重放文字角色与等宽数字——不重放时控件会停在旧档的字体与字重上。

**样式表不铺裸 `QWidget`**：底色只给具名的容器（`#appRoot` 顶层窗口、`#pageCard` 设置页、
`#hudRoot` 录屏控制条、`#AnnotationToolbar` 标注工具栏）。给裸 `QWidget` 铺底等于让每个子控件
各画一块自己的底，表单里会排出一条条灰带。
QWidget 子类要吃样式表的背景必须 `setAttribute(Qt::WA_StyledBackground, true)`，
上面这四个具名容器都是子类，所以各自带这一行。

## 1.5 视觉语言：微明度分层

浅色与深色两档共用同一套骨架，只换色值：

- 分层：窗口底 `windowBackground`、卡片 `surface`、控件 `controlFill` 三档之间只差一点明度；
  悬停 `hover` 与按下 `pressed` 再各加深一档；禁用与只读落 `mutedFill`，
  滑块槽与微调框上下按钮的悬停底也取它。
  卡片的圆角是 20、内边距 `kSpacingMedium`，底色差已经足够分出「这是一块阅读面」。
- 无容器边框：`#pageCard`、按钮、工具按钮、输入框（含快捷键框与标注行内编辑器）、列表、文本区一律没有可见描边。
  浅色与深色给按钮与输入类保留一条 1px 透明边框占位，让控件总高固定为按钮 30、输入类 32；
  换色只换底，不换尺寸。高对比度档的 2px 描边是契约，不按这一条归一化：
  按钮 32、输入类 34、数字框 38，由 `ThemeManagerFixture.ControlHeightsStayPinnedPerTheme` 按档钉住。
- 浮层微边缘：压在其它内容上的面板保留 1px 边缘（`borderStrong`），因为 Qt 样式表没有 box-shadow，
  浮层只能靠这道边缘与底下的内容分开。用得上的是 `QMenu`、`QToolTip`、`QComboBox QAbstractItemView`、
  `#hudRoot`、`#AnnotationToolbar` 与 Toast 气泡。
- 选中与勾选不再描边：导航当前项、列表选中项、勾选的工具按钮走 `accentSoft` 填充；
  复选框指示器勾选时走 `accent` 填充，勾本身按 `onAccent` 自绘。
- 形状：圆角 8（工具按钮）/ 12（按钮与输入）/ 16（菜单、弹层、浮条）/ 20（阅读面卡片）/ 胶囊（提示，
  999px 由 Qt 按高度收敛）。标注工具栏是 26px 紧凑档（图标 20），节距用 `kSpacingHairline`(2)；
  所有工具档的宽度 ≤1536 由 `ToolbarFixture.EveryToolStaysWithinTheCommonScreenWidth` 钉死。
- 按钮：次级按钮静息是 `controlFill` 填充，悬停 `hover`、按下 `pressed`、禁用 `mutedFill`；
  主按钮（`:default`）走 `accent` 填充，悬停走 `accentHover`，禁用也落回 `mutedFill`。
  静息按钮与卡片底必须有可测的明度差，否则「导出配置」这类次级按钮只剩一串文字
  （`ThemeManagerFixture.SecondaryButtonsStayDistinguishableAtRest`）。
- 菜单：字号 9pt（`kMenuTextPointSize`），项内边距 `4px 18px 4px 8px`，分隔线外边距 `2px 6px`；
  托盘、主窗「更多」与贴图右键菜单共用这一档。
- 焦点：输入面用双环（2px 主色内环、2px 空隙、32% 透明度的 2px 外环），按钮与复选框用单环，
  由 `Gui::FocusRingController` 的覆盖层绘制；浅色与深色的标注工具栏不装环，改用 QSS 的 1px 主色边。
  高对比度档不装覆盖层、仍走 QSS 的 2px 描边（见 §5）。
- 仅有的 rgba 例外：滚动条滑块在色板里没有角色，按 `rgba()` 字面量写在样式表里——同解门禁只扫 `#RRGGBB`，
  改它要连同样式表头注释一起看。其余线（勾选框指示器的悬停边、菜单分隔符、工具栏竖线）一律取实色角色。
- 高对比度档不随微明度分层：纯黑/纯白 + 2px 描边 + 纯黄选区；标注工具栏那处把内容尺寸各让 1px，
  节距与另两档保持同宽（否则一排工具会把宽度顶破 1536）。

## 2. 每个窗口的样式与状态

状态命名统一按 Qt 样式表的伪状态：`:hover`、`:pressed`、`:focus`、`:disabled`、`:checked`，
子控件用 `::indicator`、`::handle`。浅色与深色两份 QSS 逐条同构，覆盖同一批选择器
（`QPushButton`、`QToolButton`、`QLineEdit`/`QComboBox`/`QSpinBox`、`QCheckBox`、`QSlider`、`QMenu`、
`QToolTip`、`QListWidget`、`QTextEdit`、`QScrollBar`，以及具名容器 `#appRoot`/`#pageCard`/`#hudRoot`/
`#AnnotationToolbar`/`#toolbarSeparator`/`#annotationTextEditor`/`#settingsScroll`/`#settingsNav`/
`#hotkeyDisplay`/`#annotationColorButton` 与文字角色 `#pageHint`/`#metricValue`），差异只在色值。
下表的悬停、选中与焦点描述默认指浅色与深色两档。高对比度档覆盖同一批选择器，但选中与焦点改由描边承担：
导航与列表的选中项换主色边，标注工具栏与其他工具按钮的选中/聚焦、输入类与勾选框指示器的聚焦都是
2px 主色描边，快捷键框在录制态换 2px 主色描边；这一档不装焦点环覆盖层，滑块仍走手柄反转（见 §5）。
`#settingsScroll` 是设置页那一层滚动区：它自己与视口都要透明，否则卡片 `#pageCard` 四周会多出一圈与主题不合的底色。
应用里没用到的控件（`QGroupBox`、`QRadioButton`、`QProgressBar`、`QTabBar`）刻意不写规则——
没人吃的样式表和没人吃的配置键一样是债。
其中 `#annotationTextEditor`（标注窗里那个多行行内输入框）单列一档而没并进 `QTextEdit` 那条：
识别结果区的规则带 8 像素内边距，而标注输入框的上下内边距必须为零——它的高度是代码按行数算的
（`AnnotationView::resizeTextEditorToContent` 只补边框与余量），有上下内边距就会把行裁掉半截。

| 窗口 | 类与文件 | 样式来源 | 状态 |
| --- | --- | --- | --- |
| 主窗口（设置 / 历史 / 关于入口） | `src/Main/MainWindow.cpp` | 根 `#appRoot` 铺窗口底；顶部动作条 `#actionBar` 只在托盘不可用时出现——带热键提示的主操作是图标按钮 `QPushButton`，其余条目收在一个 `QToolButton` 的弹出菜单「更多」里，两张清单都取自托盘那张表（`visibleTrayMenuEntries`）；托盘可用时逐屏截图走托盘子菜单。下面是左侧导航 `QListWidget#settingsNav`（宽 `kNavColumnWidth`=160，无框）+ `QStackedWidget` 十页，每页是一张 `#pageCard` 卡片，页内 `kSpacingMedium` 边距、表单纵向与横向间距都是 `kSpacingSmall`(8)、字段宽 ≤ `kFieldMaximumWidth` | 导航项选中走 `::item:selected`（`accentSoft` 填充、无描边），悬停走 `::item:hover`（`hover` 填充）；每页顶部是 `#pageTitle`（15pt 半粗，字号由 `applyTextRole` 给）加一条 `#pageHint`（10pt 正文档、次要色，只压色不减号）；换主题时已建好的控件随 `allWidgets` 重设调色板并重放排版 |
| 标注窗 | `src/Gui/Annotation/AnnotationWindow.cpp` | 画布 `AnnotationView` 自绘底图与图形；四周 2px 主色提示线与八枚抓点（「选择」档）只上屏、不进成品；工具栏另计 | 快捷键动作表（复制/保存/贴图/识别/撤销重做），不可撤销时按钮 `:disabled`；选择档拖边框＝重裁截图范围，途中用整屏冻帧预览、松手从同一张冻帧裁，已有标注随裁切平移、撤销栈清空 |
| 标注工具栏 | `src/Gui/Annotation/AnnotationToolbar.cpp` | `#AnnotationToolbar` 卡片（1px `borderStrong` 微边缘）+ `kSpacingHairline` 间距与 26px 按钮档（图标 20；所有工具档宽度 ≤1536 由用例钉死），段与段之间是 `QFrame#toolbarSeparator`（`border` 分隔线）；`kToolbarSliderWidth`=56、字体下拉上限 120；颜色只占一枚按钮，弹出菜单里才是 `kAnnotationPaletteColors` 八色块、`RecentColors.h` 的 `kRecentColorCapacity`(6) 最近用色与「自定义颜色…」；最右是关闭标注的 `X`（与 `Esc` 同一条收场路） | 默认是「选择」档（`V`，不落笔；点中已有图形才选中、按住可拖动），当前工具按钮 `:checked`（`accentSoft` 填充、无描边），线宽/字号只走阶梯；颜色按钮上的色块即当前形状的色，滚轮落在它上面才调不透明度；可见段按当前工具收放（`updateContextVisibility`）。这一档不装焦点环覆盖层（6px 外扩会压到相邻的 26px 按钮），聚焦只把 1px 透明占位边换成主色，尺寸不跳 |
| 截图遮罩 | `src/Gui/Capture/CaptureOverlay.cpp` | 自绘：`kCaptureMaskColor` 压暗背景、`kSelectionBorderWidth`+`kAccentColor` 选区框、`kCrosshairColor` 十字虚线、读数用 `kHintFontSize`/`kHintPadding` 压在 `kCaptureHintBackgroundColor` 上 | 光标亮过之后放大镜一直跟着（`isMagnifierVisible()`，悬停、拖框、拉手柄三处同一判据），读数条报坐标与颜色、裸按 `C` 轮换颜色写法（只影响本次）；拖拽中另显示尺寸与坐标读数；`Esc` 取消，鼠标拖框松手即交出（键盘造的选区仍走 `Enter`/双击）。重裁范围在标注窗里做：默认「选择」档拖四边/四角，松手按新范围重抓。这一层不加任何动画 |
| 贴图窗口 | `src/Gui/Pin/PinWindow.cpp` | 无边框、置顶；悬停或正在拖手柄时画 1px 主色边框（`kBorderWidth`），被选中时画 2px（`kSelectedBorderWidth`），平时一张干净的图不带装饰 | 缩放（滚轮）、旋转、透明度、鼠标穿透、右键菜单；键盘 `R`/`Shift+R` 转正一挡、`0` 与左键双击回到原始比例与角度（两处共用 `resetScaleAndRotation()` 这一个出口）、`Esc` 关闭；`Qt::Tool \| FramelessWindowHint \| WindowStaysOnTopHint`；穿透态下不抢焦点。松手吸附屏边、贴边收起与悬停展开走 `kSnapSpring`（过阻尼、单调逼近，约 250ms）；拖动与改大小逐帧跟手，按下即停掉进行中的弹簧，快速进出从当前几何重定向，标注期间不收起 |
| 识别结果窗 | `src/Gui/Ocr/OcrResultWindow.cpp` | 根 `#appRoot` 铺底，`kSpacingLarge` 边距 + `kSpacingMedium` 分栏间距；预览区最小尺寸 `kPreviewMinimumWidth`/`kPreviewMinimumHeight` | 左侧原图右侧文本，点一行在图上圈出该行；置信度那一格在后端给不出时显示「未提供」（不是 0%）；摘要读数上 tabular figures |
| 录屏控制条 | `src/Gui/Record/RecordingHud.cpp` | 无边框悬浮卡片 `#hudRoot`（1px `borderStrong` 微边缘，Qt 的样式表没有投影）：`kHudEdgeGap` 屏幕边距、`kSpacingSmall`/`kSpacingTiny` 内间距、`kCountdownFontPointSize` 倒计时字号 | 倒计时→录制中→暂停→停止；按钮可用态随服务状态切换；时长那一格是 `#metricValue`（13pt 半粗 + 等宽数字，秒跳不抖宽）；控制条本身不加动画 |
| 取色器 | `src/Gui/Pick/ColorPickerOverlay.cpp` | 自绘放大镜：`kMagnifierSampleEdge`/`kMagnifierViewEdge` 取样与显示格数、`kMagnifierPenWidth` 边框、`kInfoStripHeight` 信息条 | 逐像素网格 + 十字线；颜色格式按 `Pick` 的词表切换；取色时按物理像素取样再折算；HEX/RGB 读数上 tabular figures |
| Toast 通知 | `src/Gui/Controls/Toast.cpp`、`StreamingText.cpp` | 用 `palette.success`/`palette.danger`/`palette.secondaryText` 从色板角色现算内联样式（`success` 与 `danger` 不进 QSS，用例按消费方名单核对）；气泡走 `surface` 底 + 1px `borderStrong` 边缘 + 胶囊圆角（999px 由 Qt 按高度收敛）；图标与文字同一行，图标底边落在首行基线下方 0.125em | 三类：Information / Success / Danger，3 秒后自收。成功与信息提示逐字显现（第 i 字在 i×30ms 起步、单字 90ms 从模糊到清晰，缓动 `cubic-bezier(.215,.61,.355,1)`），错误提示整句即时出现；成功图标从 0.6 弹到 1.0；图标描边取该条提示的语义色；新提示顶掉旧提示时覆盖层一并销毁重启，不叠影 |
| 复选框（设置页全部勾选项） | `src/Gui/Controls/CheckBox.cpp` + QSS `QCheckBox::indicator` | 方框的填充、圆角与悬停描边仍由 QSS 画：静息 `controlFill`、悬停 `hover` + `borderStrong` 边、勾选 `accent` 填充、禁用 `mutedFill`；勾由 `Gui::CheckBox` 按 `onAccent` 自绘，QSS 用 `[animatedIndicator="true"]` 把静态勾图关掉 | 勾选时从 0.55 弹性到 1.0（`kPopSpring`，约 450ms、过冲约 19.5%），取消勾选不弹；三档的勾都取 `onAccent`（高对比度档是蓝底白勾，与另两档同一套取色）；减少动效时直接到 1.0 |
| 焦点态（所有可聚焦控件） | `src/Gui/Controls/FocusRing.cpp` | `FocusRingController` 作为应用事件过滤器，首次聚焦时创建覆盖层；宿主是能装下目标矩形外扩 6px 的最近祖先，几何按宿主坐标系算 | 输入框（`QLineEdit`、`QComboBox`、`QSpinBox`、标注行内 `QTextEdit`、`HotkeyEdit`）双环；按钮、工具按钮、复选框单环（复选框套在 indicator 上）；可编辑下拉框/微调框内部的行编辑映射回外层；只读、禁用与标注工具栏内的控件不装；滑块走手柄反转（见 §5）；切到高对比度档立即撤环、切回时给当前焦点补环 |
| 快捷键输入框 | `src/Gui/Controls/HotkeyEdit.cpp` | `QLabel#hotkeyDisplay` 与 `QLineEdit` 同一套 `controlFill`/圆角/内边距/1px 透明占位边；录制态按 `[recording="true"]` 换 `accentSoft` 底与主色边（高对比度档不换底：仍是纯白，只把 2px 描边换成主色） | 闲置态显示已存的组合，悬浮说明写清「点击这一格后按下组合键即可替换，按 Esc 取消」；进入录制态换成「请按下快捷键（Esc 取消）」的提示 |
| 数字输入框 | `src/Gui/Controls/SpinBox.cpp` | 继承 `QSpinBox`，关掉原生上下按钮；右缘叠两枚自绘 chevron，悬停给一档 `mutedFill` 底 | 点击加/减，范围、后缀、`不限` 等语义仍由 `QSpinBox` 负责；键盘上下键照常；数值上 tabular figures |
| 历史列表 | `src/Gui/History/HistoryView.cpp` | 缩略图边长 `kThumbnailEdge`、圆角 `kThumbnailCornerRadius`；列表项形状与导航共用 `QListWidget::item` | 顶部一枚搜索框（按文件名、类别或日期）加一个类别筛选下拉，次序固定时间倒序；保留策略本身在「通用」页那两格，这一页不重复摆读数。缩略图读不出来时给一枚类别图标（`Record`/`CaptureRegion`），不留空白方块；时间与尺寸读数上 tabular figures |

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
| icon | `#6B6B66` | `#8A8A84` | 纯黑 | SVG `currentColor` 的替换色 |
| success / danger | `#3B7D21` / `#CA3932` | `#9BE35A` / `#FF7A71` | `#006400` / `#B00000` | Toast 与状态色（不进样式表） |
| accentHover / accentSoft | `#1D61D5` / `#E5EEFA` | `#3D7FF2` / `#22344D` | `#08449E` / `#D6E4FB` | 主按钮悬停 / 导航当前项与勾选的工具按钮 |

浅色与深色里不出现不透明纯黑；半透明色只出现在截图遮罩（`kCaptureMaskColor`、`kCaptureHintBackgroundColor` 与 `kCrosshairColor`，这三支是固定值、不随主题换）与滚动条滑块——滑块的 rgba 在浅色档是半透明黑、深色档是半透明白（`LightAndDarkSheetsUseNoOpaqueBlack`）。
对比要求由用例算：`ThemeManagerFixture` 里的 `contrastRatio` 逐角色核 WCAG——正文压在本档
窗口底与卡片底上要 ≥ 4.5（AA），**次要文本三档都 ≥ 7.0（AAA）**（说明行与正文同字号，弱化只能靠色），
焦点环/主按钮描边与图标色 ≥ 3.0（非文本对比），选区上的正文与主按钮上的文字 ≥ 4.5；
状态色另核：次要文本压 `hover`/`pressed`/`mutedFill`/`controlFill` ≥ 4.5，正文与主色描边压 `accentSoft`
分别 ≥ 4.5 与 ≥ 3.0（选中项的底与它自己的强调色不能糊成一片），主按钮悬停时 `onAccent` ≥ 4.5；
高对比度档另加：正文对窗口底、次要文本对卡片底、选区上的正文都 ≥ 7.0（AAA 级）。

「截图对比」这一条给的是可复现的做法与一条渲染判据，而不是静态图片：

- 人工核：设置 → 主题 三档各切一次（切换即时生效），主窗口、标注工具栏、截图遮罩、Toast 四处在
  每档下看一遍；色值差异以上表为准。
- 机器判据：`ThemeManagerFixture.WidgetsRenderThePaletteSurfacePerTheme` 把同一张 `#pageCard`
  分别按两份样式表渲染成位图，取中部像素，断言浅色画出 `kLightPalette.surface`、深色画出
  `kDarkPalette.surface`，且两档像素不同。卡片就是 `surface` 角色本身；按钮平时是 `controlFill`，
  拿它取中部像素测的是另一档角色。样式表没挂上时（实测把渲染改成空样式表）三条断言同时红。
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
  所以这一版把颜色/形状留在 QSS、把字号交给 `applyTextRole`（用例 `Style.SourceConventions` 不管这条，
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
  与控件顺序体现，`QPushButton:default` 给主操作单独配色。
- 焦点可见性是硬要求，分三档落地：
  - 浅色与深色：输入面（行编辑、下拉框、微调框、标注行内编辑器、快捷键框）用双环——贴控件边缘的
    2px 主色内环、2px 空隙、落在 4–6px 处、32% 透明度的 2px 外环；按钮、工具按钮与复选框用单环，
    复选框的环套在 indicator 上而不是整行文字。环由 `FocusRingController` 的透明覆盖层自绘，
    像素判据在 `tests/Gui/Controls/TestFocusRing.cpp`。
  - 高对比度：不装覆盖层，QSS 的 2px 描边承担焦点，`ThemeManagerFixture` 核样式表文本（`2PX SOLID`）。
  - 标注工具栏（浅色与深色）：26px 紧凑档不装 6px 外扩的环，聚焦只把 1px 透明占位边换成主色，尺寸不跳。
- 双环不用 QSS `outline`：`outline` 只在 `PE_FrameFocusRect` 分支里落笔，`QLineEdit` 不走这条路；
  Light/Dark 因此删掉了全局 `QPushButton:focus`/`QLineEdit:focus` 这类描边规则，只保留标注工具栏
  那一条紧凑边缘。高对比度 QSS 不动。
- 滑块有一条实现约束：QSS 的控件级边框在 `CC_Slider` 的自绘路径里不落笔（换背景色、
  补 `WA_StyledBackground` 都不救），焦点只能挂在 `::handle:horizontal:focus` 上做反转
  （底色芯 + 正文色圈）；三份样式表都这么写，判据是 `CheckboxAndSliderShowAKeyboardFocusRing`
  里数手柄矩形内的主色像素。别把它「统一」回 `QSlider:focus` 的写法。
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
- 图标全部原创 SVG（24 视框、`currentColor` 描边），描边色由 `IconProvider` 按主题注入；
  用例 `IconProviderFixture` 数的是真的画出来的像素，不是文件名。整套图的画法规格：
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
    （判据把 2×/3× 两档都钉住）。
