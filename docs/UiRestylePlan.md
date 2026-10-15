# 界面换肤计划：移植 scaffold-vue 的「珍珠玻璃」设计

> 用量与立场：**只换视觉，不改交互与结构**。色板、材质分层、圆角、控件形态、深色映射照源抄；
> 版式骨架、动效与 web 专属表达不抄（见 §一）。全部数值以源仓库为准，本计划冻结的是「抄什么」与
> 「怎么换算」，实施时逐条对照源文件读值。
>
> 来源：`G:\Codes\scaffold\DESIGN.md`（规范口径）与 `G:\Codes\scaffold\scaffold-vue\src\styles\`
> （tokens / base / materials / components 四层取值）。目标：`docs/UiStyleGuide.md` 描述的那套
> 三档主题体系（`src/Ui/DesignTokens.h` + `Resources/Styles/*.qss` + 自绘面 C++ 取色）。
>
> 硬边界：本仓已有门禁（对比度、token↔QSS 同解、三份 QSS 同构、像素渲染、标注工具栏宽度 ≤1920）
> 一条都不许破——新值先现算再落笔。

## 一、范围裁决（抄什么、不抄什么）

**抄：**
1. 色板与语义：五档字色（ink/muted/soft/faint）；蓝 `#236de9` = 选择与主操作、青柠 = 正常状态、
   红 = 危险；`--shade` 派生的细边线（.085 / .11）。
2. 圆角阶梯：8 / 12 / 16 / 20 / 24 + 胶囊 999。
3. 材质分层：阅读面（m-surface）、玻璃块（m-pill）、浮层（m-float）、槽（m-groove）、
   深色铭牌（plate）——以及它们的表达手段（细描边 + 渐变 + 顶边内高光）。
4. 组件取值：按钮五族、输入壳、勾选框、下拉菜单与选项、提示胶囊、滚动条、面板圆角。
5. 深色档：同一套语义换材质（tokens.css 的 `[data-theme="dark"]` 段全量映射）。
6. 密度取向：控制高 36（标准）/ 30（小档）、行高节奏、字重 620/660（落本仓 DemiBold）。

**不抄：**
1. 版式骨架（rail / topbar / 表格 / 看板 / 图谱 / 登录页）：本应用没有这些页面，只借
   「外壳—阅读面—浮层」的材质关系，页面结构维持本仓现状。
2. 动效：GSAP、`--ease-premium`、弹簧、打字机、级联——需求 §九 禁止动画干扰截图/标注/录屏，
   本仓全无动画，维持；`@property` 补间一并不搬。
3. web 专属表达：`backdrop-filter`、多层 `box-shadow`、`inset` 阴影、指针光 `.m-glow`、
   噪点颗粒层、斜线填充 `--hatch`——换算见 §三；玻璃模糊直接用**设计自带的降级档**
   （materials.css 的 `prefers-reduced-transparency` 段：改实色面）。
4. 仪器化语汇（眉标/读数/铭牌/刻度环/神经网络/图谱）：本应用无仪表盘。**唯一例外**：
   截图遮罩的读数条借 `--plate-*` 深色铭牌那一档（它本来语义就是「读数压在任意画面上」），
   作为可选点，出图定。
5. 高对比度档：web 无对应物；本仓无障碍契约档位，**维持不动**（只按新角色补映射条目）。
6. 标注色板 `kAnnotationPalette` 八色：那是用户画图的颜色，不是皮肤，保留。

## 二、源盘点（照抄清单）

权威口径 `DESIGN.md`；取值 `scaffold-vue/src/styles/`。核心值（浅色基线；深色档逐条对应，实施时照抄）：

| 源令牌 | 浅色值 | 深色值 | 用途 |
| --- | --- | --- | --- |
| `--ink` | `#101418` | `#eef2f1` | 正文 / 标题 |
| `--ink-muted` | `#596368` | `#b0babd` | 次级正文 |
| `--ink-soft` | `#7e888c` | `#8d979a` | 标签 / 图标 |
| `--ink-faint` | `#99a2a5` | `#6c7679` | 元信息 / 占位 |
| `--blue` / hover | `#236de9` / `#1d61d5` | `#4c8dff` / `#3d7ff2` | 选择、主操作 |
| `--blue-soft` | `rgba(35,109,233,.1)` | `rgba(76,141,255,.16)` | 选中底 |
| `--lime` / `--leaf-text` | `#82dd2d` / `#58a021` | 同 / `#9be35a` | 状态点 / 状态文字 |
| `--red` / `--red-text` | `#ff5a50` / `#e0443b` | 同 / `#ff7a71` | 危险填充 / 危险文字 |
| `--focus` | `rgba(35,109,233,.8)` | `rgba(76,141,255,.85)` | 焦点环 |
| `--line` / `--line-strong` | `rgba(46,58,62,.085)` / `.11` | 白系 `.07` / `.1` | 细边线 / 控件描边 |
| `--hover-bg` / `--press-bg` | `rgba(46,58,62,.045)` / `.075` | 白 `.05` / `.08` | 悬停 / 按下 |
| `--stage` / `--canvas` | `#dfe3e2` / `#f6f8f7` | `#090b0c` / `#121517` | 外壳底 / 内容底 |
| `--surface` / `--surface-solid` | 渐变白 → `#fbfcfc` | 深渐变 → `#1b1f21` | 阅读面 |
| `--input-bg` | `rgba(255,255,255,.78)` | `rgba(255,255,255,.04)` | 输入底 |
| `--groove-bg` | `rgba(225,231,229,.5)` | `rgba(0,0,0,.22)` | 凹槽 / 禁用底 |
| `--overlay` | `rgba(16,20,24,.26)` | `rgba(0,0,0,.5)` | 遮罩 |
| `--plate-bg` / `--plate-ink` | `rgba(21,24,26,.9)` / `#f2f5f4` | `rgba(8,10,11,.78)` | 读数铭牌 |
| `--r-xs…xl` | `8 / 12 / 16 / 20 / 24` | 同 | 圆角 |
| `--control-h` / `-sm` | `36 / 30` | 同 | 控件高 |
| `--e1/e2/e3` | 双层接触+环境阴影 | 黑系 | 材质高度（换算见 §三） |
| `--edge` | `inset 0 1px 0 rgba(255,255,255,.92)` | 白 `.06` | 顶边内高光 |

组件取值（components.css）：按钮 `h36/圆角 12/padding 0 15/字重 620`（小档 `h30/r8/0 11`）；
图标按钮 `32×32 r8`（小档 28）；输入壳 `h36/r12/0 12/1px line-strong/顶高光`，
focus = 2px 蓝环 offset 2（换算见 §三）；下拉菜单 `r16/padding 6/选项 min-h34 r8`；
勾选框 `17×17 r5`，选中蓝底白勾；提示胶囊 `h44/padding 8 8 8 14/999 圆角`；
面板 `r20`，头 `min-h60/padding 14 20`；标签 `h22/999`；滚动条细、`rgba(shade,.16)`、圆角 999。

## 三、web → Qt 换算表（可表达性先过关）

| web 表达 | Qt 手段 | 可信度 | 备注 |
| --- | --- | --- | --- |
| 实色 / `rgba()` 填充 | QSS `rgba()` | 1:1 | |
| `linear-gradient(145deg)` 面 | QSS `qlineargradient` | ≈ | 角度换坐标；渐变两端照抄 |
| 多层 `box-shadow`（e1/e2/e3） | **顶层浮窗**：`QGraphicsDropShadowEffect`（真阴影，blur/offset/color 三参对齐 e 档）；**窗内阅读面**：1px 细描边 + 顶边内高光 + 底色差 | ≈ / 需探针 | P3 探针出图定案；窗内不逐面挂 effect（子控件渲染路径与性能） |
| `--edge` 顶部内高光 | `border-top: 1px solid rgba(255,255,255,·)` | ≈ | QSS 支持单边描边 |
| `backdrop-filter: blur()` | 设计自带降级档（实色面） | ✓ | 设计已内建，不算损失 |
| `outline: 2px + offset` 焦点环 | 候选 A：`border: 2px solid 焦点色`；候选 B：双描边+内留白模拟外环 | ± / 需探针 | P3 出图定；高对比度档的既有 2px 判据照跑 |
| `opacity: .5` 禁用 | 色值降档（本仓既有 mutedFill / secondaryText 路线） | 等价语义 | QSS 无 opacity 属性 |
| `box-shadow: 0 0 0 Npx` 光晕 | 省或画淡色外圈 | ± | 状态点不承载语义时可省 |
| px 字号 | 本仓规范：字号走 `QFont` 点值三档（Body=10pt），**不进 QSS** | ✓ | Vue 13px ≈ 本仓 Body 档 |
| `font-weight: 620/660` | `QFont::DemiBold` | ≈ | 本仓已有 |
| `tabular-nums` | 本仓 Metric 角色已带 | ✓ | |
| `border-radius: 999px` | QSS 大半径 | ✓ | |
| `mix-blend-mode` / `filter` / `repeating-linear-gradient` | 不搬 | — | 见 §一 |

## 四、令牌映射（Vue → 本仓十七角色）

浅色照搬、深色照搬，各角色定义落在 `src/Ui/DesignTokens.h` 的 `ThemePalette`（字段一个不加、
一个不减——QSS 与绘制代码只认既有角色名，共十七个）。逐条：

| 本仓角色 | 取源 | 备注 |
| --- | --- | --- |
| `windowBackground` | `--stage`（外壳）/ `--canvas`（内容面）二选一或两级 | 本仓只有一个角色：取 `--canvas` 系（内容面为主），外壳边缘层次靠容器描边表达 |
| `surface` | `--surface` 渐变（QSS 面）/ `--surface-solid`（C++ 取色处） | 角色值给 `--surface-solid` 的同系实色，渐变只进 QSS 面 |
| `text` | `--ink` | |
| `secondaryText` | `--ink-muted` | ⚠️ 粗算 5.8:1（对 `#f6f8f7`）< 本仓 7:1（AAA）——**实施时现算压深**（保色相），不达标不上 |
| `border` / `borderStrong` | `--line` / `--line-strong` | 合成实色以过「token↔QSS 字面量同解」判据 |
| `accent` / `accentHover` / `accentSoft` | `--blue` / `--blue-hover` / `--blue-soft` | accentSoft 落 rgba 底 |
| `onAccent` | 浅 `#fff`；深档**现算** | 深档 `#4c8dff` 压白字与压深字都要算，取过 4.5 的那一支 |
| `selection` | `--blue-soft` 加强版 | 正文压其上要过 4.5，现算 |
| `icon` | `--ink-soft` | 非文本 ≥3.0 现算 |
| `success` | `--leaf-text`（承载文字）+ `--lime`（点/描边，另立 C++ 常量） | `#82dd2d` 压白字对比不够，不许当填充承载文字 |
| `danger` | `--red`（填充）/ `--red-text`（文字） | 两用途分两支 |
| `mutedFill` | `--groove-bg` | |
| `hover` / `pressed` | `--hover-bg` / `--press-bg` | |
| （C++ 常量）遮罩读数 | `--plate-bg` / `--plate-ink` | 可选档，出图定 |
| （C++ 常量）标注/选区自绘 | `kSelectionBorderWidth` 等维持；色取 `--blue` | 选区框、十字线、光标高亮随主色 |

## 五、面与组件映射

**材质 → 本仓具名面：**

| 材质 | 本仓面 |
| --- | --- |
| m-surface 阅读面 | `#appRoot`、`#pageCard`、OCR 结果窗、贴图组管理窗、设置页卡片 |
| m-pill 玻璃块 | 导航选中项、标注工具栏按下态按钮、次级按钮悬停、分段控件 |
| m-float 浮层 | `#AnnotationToolbar`、`#hudRoot`、QMenu/托盘菜单/更多菜单、Toast（换胶囊） |
| m-groove 槽 | 滑块槽、禁用输入底、搜索框底 |
| plate 铭牌 | 截图遮罩读数条（可选） |

**组件 → 控件：**

| Vue 组件 | 本仓控件 |
| --- | --- |
| `.btn` 五族（primary/secondary/ghost/text/danger） | `QPushButton`（默认=secondary、`:default`=primary、`QPushButton#dangerButton` 等具名档），图标版同族 |
| `.icon-btn` 32 / sm 28 | `QToolButton`（**但标注工具栏保持 B 批的紧凑档与 ≤1920 宽度契约**，不跟着放大） |
| `.input-shell` | `QLineEdit`/`QComboBox`/`QSpinBox`/`QFontComboBox`/`QTextEdit`（高 36、r12、rgba 底、hover 描边加深、focus 环） |
| `.select-menu/-option` | `QComboBox` 弹出列表与 `QMenu`（Qt 原生弹出：做 palette + QSS 面，不做自绘） |
| `.checkbox` 17r5 | `QCheckBox`（白勾用本仓烘焙 SVG 路线换蓝底两份） |
| `.toast` 胶囊 | 本仓 `Toast`（胶囊 + 状态点 + 停留时长不变） |
| `.panel` r20 | `#pageCard`（圆角 12→20） |
| 滚动条 | `QScrollBar` 三份 QSS（细、低对比、圆角胶囊） |
| `.modal` | 系统对话框维持原生（只 palette），不做自绘模态 |

## 六、实施阶段

- [x] **P0 映射表冻结**：冻结值落成下面的附录（浅/深逐角色 + 压深差异）；设计原值与合成口径一并记录。
- [x] **P1 令牌重定**：`DesignTokens.h` 两档重写（HC 不动），`kAccentColor`、读数铭牌常量、标注色板
  「主色蓝」跟随；`TestThemeManager` 像素探针改挂阅读面卡片（按钮在新皮肤里是透明底）。
  验收达成：`TestUi` 50/50 全绿。
- [x] **P2 QSS 重写**：三份逐条重写（圆角 8/12/16/20、控件高 36、透明次级按钮、菜单 16、胶囊提示、
  细滚动条）；Toast 胶囊圆角；HC 保持 2px 描边语言并把标注工具栏内容尺寸各让 1px
  （满配色板宽度 1994→1902，≤1920 契约保住）。验收达成：三档出图肉眼过 + 工具栏读数 1902。
- [x] **P3 材料探针**：定案——焦点环取「2px 主色描边」近似设计的外环（QSS 没有 outline，
  双描边方案不可表达）；输入面取实色 surface + borderStrong（近白底上渐变不可辨）；
  **真阴影本轮不挂**（半透明窗体 + 内边距手术会动工具栏几何与宽度契约，收益/风险比不合算），
  列为后续候选。
- [x] **P4 全窗出图对照**：十张文档图按新皮肤重出；`UiAuditTool` 补深色/高对比度矩阵面；
  `UiStyleGuide.md` 已按新口径改写（新增 §1.5 视觉语言、§3 色表全量替换）。
- [x] **P5 收口**：全量门禁 927/927 全绿（`GATE_EXIT=0`）；提交按模块三笔（换肤/钉样台/文档）。
  观感条目的新皮肤重验归入 `UiAuditPlan.md` 的 P4/P5 执行——那份计划顶部已加交叉说明。

## 七、验收判据（换肤批次通用）

1. `ctest` 全绿：对比度（正文/次要/非文本/高对比度档）、token↔QSS 同解、三份 QSS 同构、像素渲染。
2. 十张文档图新皮肤重出且人眼过；三档主题 × 主要浮窗矩阵出图无「文字读不出、禁用态无反馈」。
3. 标注工具栏宽度契约（每档 ≤1920）四条判据不复红。
4. QSS 零禁用特性：`box-shadow` / `transition` / `backdrop-filter` / `outline` / `@property` /
   `font-size` 在源文件里零命中（`Style.SourceConventions` 扩一条扫描把新写法挡在门外）。
5. 高对比度档仍满足纯黑/纯白 + 2px 描边判据（一个字节不动地过）。

## 八、边界与取舍

- **表单骨架不动**：设置页保持「左标签右字段，右对齐缝恒定 9px」（上一批刚按反馈定稿）；
  Vue 的「标签上置」版式列为候选，P4 出一张对照图再议，不默认改。
- **控制高**：抄 36（标准）/ 30（小档）；设置页行距维持本仓紧凑取向（曾按反馈收紧），不跟着放松。
- **禁用态以色代 opacity**；**阴影只给顶层浮窗**（窗内不挂 effect）。
- **高对比度档、Linux 降级行为、系统对话框**：不动。真面板高 DPI 观感照旧标注「未验」（只有 dpr1 屏）。
- 换肤不碰任何行为逻辑；动到 QSS/令牌之外的代码只许是取色常量。

## 九、与既有台账的关系

- `docs/UiAuditPlan.md` 的**观感类**条目（UI-21/25/31/32/50/52/53…）在换肤后按新皮肤重验，
  不在旧皮肤上先查一遍再重做；**几何/功能类**（组 1 表单行、组 2 摆放与重摆、组 3 提示完备性、
  组 4 浮窗族行为）与皮肤无关，可并行照原计划跑。
- 标注工具栏的 ≤1920 宽度契约是本次换肤的硬边界：P2 改 QSS 后必须复跑那四条判据。
- 图标集维持既有契约（24 视框 / 1.5 线宽 / currentColor），只换 `icon` 角色色值；
  勾选对勾按本仓烘焙路线换蓝底两份 SVG。

## 附录：冻结值表（P0 现算）

计算口径：按 WCAG 相对亮度逐对现算（与 `ThemeManagerFixture.contrastRatio` 同式）；
不达标的设计原值只等比压暗（保色相），每处留 ≥0.2 余量防舍入。设计原值与冻结值的差异只有四处：

| 角色 | 设计原值 | 冻结值 | 原因 |
| --- | --- | --- | --- |
| secondaryText（浅色） | `#596368` | `#495358` | 压底色只有 5.8:1 < 本仓次要文本 7:1（AAA） |
| success（浅色） | `#58A021` | `#3B7D21` | 当文字压底色 3.3:1 < 4.5 |
| danger（浅色） | `#E0443B` | `#CA3932` | 当文字压底色 4.2:1 < 4.5 |
| onAccent（深色） | 纯白 | `#0B1220` | 深档主色 `#4C8DFF` 压白字只有 3.2:1，压深字才够 AA |

其余角色照抄，或按设计给定的合成规则把半透明线合成实色六位（合成式 `round(fg*a + bg*(1-a))`）：
`border` = `--line-strong`(.11) 合成；`borderStrong` = shade .18 合成；`hover`/`pressed`/`mutedFill` /
`accentSoft`/`selection` = 对应 rgba 合成。全部对比对逐条过门禁，最紧的一对是浅色
`secondaryText / windowBackground` = 7.40（门禁 7.0）。逐角色值见 `UiStyleGuide.md` §3。
