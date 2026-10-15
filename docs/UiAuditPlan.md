# 界面审查计划

> 这份计划回答一个问题：界面上「哪些问题确实存在」。每条给出**假设 / 线索 / 判定标准 / 取证方式**，
> 执行时逐条回填判定。立场：只审不改——查实的问题先落「判定 + 证据 + 建议方向」，
> 修复按批次另走正常开发流程，且当批把判据落成常驻用例（别只留一句「已人工看过」）。
>
> 基线环境：本机 2560×1440、100% 缩放（`GetDpiForSystem()=96`）、单屏 dpr 1.0；
> 界面为三档主题，八个设置页共用一套表单骨架（`src/App/MainWindow.cpp` 的 `makePageForm`）；
> 历史页与关于页是自定义版面，不走这套表单。

## 一、用户直报两条：已实测（判定：属实）

### A. 设置页「文字和后面的输入框没有对齐」

实测数据（2026-10-15，对 `docs/images/main-*.png` 逐行像素量尺）：

- 标签一律**左对齐**于 x≈224（与页标题、说明行同一个左缘）；同页内控件列固定（通用页、截图页 x=352）。
- 标签右端到控件的**缝随标签字数变化**：缝 ≈ 129 − 12×字数（px）——
  4 字 81px、5 字 69、6 字 57、7 字 45、8 字 33、10 字 9。跨页更参差：快捷键页几乎全贴着（缝 2–3px），
  标注页最高 113px。
- 成因在 `src/App/MainWindow.cpp:164`：`form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter)`。
  QFormLayout 的标签列宽 = 本页最长标签的宽度，标签左对齐之后，短标签那一行整行留空。
- 同一处注释（`MainWindow.cpp:163`）写着「右对齐会让标签与字段之间那条缝忽宽忽窄」——
  **与 Qt 实际行为相反**（标签右对齐时缝恒定，= QFormLayout spacing 8px；左对齐才会忽宽忽窄）。
  实现与注释的关系要一并更正。

判定：属实。用户感知 = 标签与自己的输入框不成对；且与文件里自述的设计意图相左。

建议方向（先出 PNG 对照再定案）：`setLabelAlignment(Qt::AlignRight)`，缝恒定 8px，
与 Windows 设置对话框惯例一致。备选：保持左缘齐、把标签列收窄到固定宽（短标签行仍会留白，只解决一部分）。

影响面：8 个设置页（通用/截图/标注/贴图/录屏/文字识别/快捷键/主题）同改；
`UiShotTool` 的十张文档图里 7 张像素有变、需重出。

**已修**：`setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter)`——缝恒定 9px（含字形右边距），
快捷键页上「固定行 / 自定义行」的字段列与按钮齐右缘、跨列的分组标题行不受影响。
判据落成 `MainWindowFixture.SettingsFormKeepsAUniformLabelToFieldGap`（逐行量「标签文字右缘 → 字段左缘」
的缝，跨差 ≤3px）；改回左对齐实测变红（最窄 9px、最宽 81px）。文档图 7 张已随修重出
（主题页单行两对齐同形、关于与历史页没有表单行，像素未变）。

### B. 「截图之后下面的各个选项好大好大」（标注工具栏）

实测数据（2026-10-15，UiShotTool 加临时探针离屏钉样）：

- 工具栏 `sizeHint = 2518×53`、`minimumSizeHint = 2374×50`；`setGeometry` 往 1536/1280 夹**无效**——
  Qt 拒绝缩到最小尺寸以下，实测实际仍是 2374×53。生产摆放用首选宽 2518（2560 屏上）。
- 单排常显 51 枚控件 + 4 条分隔线 + 1 个滑块：13 工具 + 8 预设色 + 最近色(≤6) + 自定义 +
  线宽滑块(64px) + 11 个工具相关开关 + 字体下拉(≤150px) + 字号下拉 + 8 个文字相关开关 + 撤销/重做 +
  5 个动作按钮。**全仓无按当前工具收敛可见段的逻辑**（`AnnotationToolbar.cpp` 里没有一处 setVisible）。
- 按钮节距 42px（图标 20px）、色块 chip 20px/42px、线宽滑块手柄 ≈30px/64px。
- 本机 2560 屏、100% 缩放下摆出来 ≈2518px ≈ **屏宽的 98%**；用户注册表已有 1 格最近色
  （`HKCU\Software\Gyanis\GyanisScribe\Pick\RecentColors`），色板与最近色随时会再加宽。
- 逻辑宽 <2374px 的屏（1920 档、2K@125%、1080p@150%……）：右端「复制 / 保存为文件 /
  贴到屏幕上 / 识别文字 / 识别二维码」整排落在屏外——不可见也不可点，是功能级后果。

判定：属实。不只是「大」，是宽度不收敛 + 无溢出兜底。

建议方向（先出两版 PNG 对照再定案）：
1. 按当前工具收敛可见段（矩形只留填充/圆角/线型；文字只留字体族/字号/粗斜/描边族……；动作区固定右侧）→ 目标 ≤ ~1200px；
2. 两行布局；
3. 溢出收进「更多」菜单。

影响面：`AnnotationToolbar.cpp` 布局结构、`AnnotationToolbarGeometry` 的摆放判据与用例、
文档图与 README 界面一览重出。

## 二、取证设施（P0 先建，其余条目都靠它）

1. **离屏钉样台**：扩 `tests/App/UiShotTool.cpp`（或新增并列的 `UiAuditTool`），给每面浮窗
   （标注工具栏、截图遮罩、放大镜、贴图窗、OCR 结果窗、录屏 HUD、Toast、菜单）出一张 PNG，
   并把 `sizeHint / minimumSizeHint / 实际 geometry` 打进 stdout。
   - 命令（本会话已验证可行）：构建 `MSYS_NO_PATHCONV=1 cmd.exe /c "build\BuildOne.bat UiShotTool"`；
     运行 `build/debug/bin/UiShotTool.exe <输出目录>`。`build/BuildOne.bat` 是会话级构建助手
     （gitignored，没了就照 `scripts/WindowsBuild.bat` 的环境变量自己拼同一条 `cmake --build`）。
   - 样本构造与生产同路：真 settings（临时 ini，不碰用户注册表）、真 `IconProvider`、
     先 `theme.applyFromSettings()` 再建控件；离屏平台下 `show() + processEvents()` 后 `grab()`。
   - 主窗口出两档尺寸：默认档 680×560（`MainWindow.cpp:1470`）与文档档 1000×680（UiShotTool 现值）。
   - 纪律：临时探针用完即撤（`git diff` 自证干净）；要长期留的判据落成用例或常驻设施。
2. **量尺脚本**（本机 Python 3.14 + Pillow 12.2.0，已验证）：逐行暗像素列段 = 左缘/缝/节距；
   色值精确匹配 = 色块 chip 定位。骨架见附录 A。
3. **实机走查的边界**（写死免得撞）：
   - 本机只有一块 dpr 1.0 的屏：125%/150% 的**真面板观感造不出**（`docs/UiStyleGuide.md` §4 已自认），
     用离屏 `QT_SCALE_FACTOR=1.25/1.5/2.0` 合成验算法形状，结论注明「算法形状，未上真面板」。
   - 系统对话框（QFileDialog、QColorDialog）出不了离屏图：只记「未验」，不冒充已查。
4. **判定纪律**：查实一条 → 留证据（PNG 路径 + 读数 + 代码行）→ 定严重度
   （功能级 = 不可达/不可读；观感级；一致性级）→ 修复批次里落像素/结构用例（照
   `tests/Ui/TestThemeManager.cpp` 的做法），复跑 `ctest` 与文档图重出。

## 三、补充假设清单（逐条待判定）

### 组 1：主窗口与设置页

| 编号 | 假设 | 线索 | 判定标准 | 取证方式 |
| --- | --- | --- | --- | --- |
| UI-01 | 跨页控件列 x 不一致（实测 346/352/361/364 浮动），翻页时字段左缘轻微跳动 | 十张文档图逐页量尺结果 | 同页内一致（现状已成立）；跨页浮动 ≤ 半个字符宽可接受，超过则记缺陷 | 量尺脚本逐页扫 |
| UI-02 | 快捷键页 18 行的缝只有 2–3px（标签几乎贴着 HotkeyEdit），与其它页观感脱节 | 十张图逐页量尺 | 与 A 的修法一并定案（右对齐后全页统一 8px） | 量尺 + A 的修法对照图 |
| UI-03 | 顶部动作栏长文案（「收起 / 摆回全部贴图」「切换到下一组贴图」）在默认 680 宽窗口下截断/挤压 | 动作栏按钮带文字+图标，默认窗口 680×560 | 默认窗口宽下无截断、无省略号 | 钉样台出 680 宽图 |
| UI-04 | 设置页滚动条出现时压住字段右缘或与卡片圆角打架 | `#settingsScroll` 自设透明，字段上限 380 | 滚动条出现/消失两种状态下字段右缘一致 | 钉样台两态出图对比 |
| UI-05 | 数字框/组合框/行内行控件（路径行）同行高度不一致，行高参差 | 截图页三类控件混排 | 同一行内控件高度差 ≤ 1px，行距均匀 | 量尺扫行高 |
| UI-06 | 双按钮行（备份与恢复、分享色板）两枚按钮与上行字段列未对齐或间距不齐 | 曾经踩过「QFormLayout 行高 15px 按钮被裁」（`MainWindow.cpp:1629` 附近注释） | 按钮左缘 = 字段列左缘，两按钮间距 = kSpacingSmall | 量尺 + 钉样台出图 |
| UI-07 | 导航列 180px 内条目文字超长（「文字识别」等）在 26 行快捷键页与选中态下无溢出 | `kNavColumnWidth=180` | 无省略号、无换行 | 逐页出图 |
| UI-08 | 关于页（自定义版面）与其它页的左缘/字号层级不一致 | 关于页不走 `makePageForm`（量尺该页标签起 = 217 而非 224） | 左缘与表单页对不上即可疑 | 量尺 + 出图 |
| UI-09 | 历史页搜索框 + 类别下拉在窄窗下的排布与列表项缩略图对齐 | `HistoryView` 自定义版面 | 无挤压、无截断 | 出图（空态与有内容两态） |
| UI-10 | 快捷键页「in-app 列表」的分组标题与行缩进层级不清晰 | 26 行同页堆叠 | 分组一眼可辨（标题 vs 行的层级） | 出图 |
| UI-11 | 设置页说明行（`#pageHint` 限宽 420）折行处的观感与行距 | 长说明行在多处 | 折行后行距 = 行高，不出现半行距 | 出图逐页核 |

### 组 2：标注窗与工具栏

| 编号 | 假设 | 线索 | 判定标准 | 取证方式 |
| --- | --- | --- | --- | --- |
| UI-20 | 色板满配（16 格）+ 最近色 6 格时工具栏再宽 ~590px（按 chip 节距 42px 算），2560 屏也会被切 | 色板上限 16、最近色上限 6（`AnnotationToolbar.cpp` 注释） | 满配宽度 ≤ 目标屏宽（否则坐实 B 的功能级后果） | 钉样台塞满配置出图 + 量宽 |
| UI-21 | 高 DPI（dpr 2）下 20px 图标与 1px 描边清晰度 | 图标按 Medium=20 固定 | 合成 dpr 2 出图看不清即为缺陷 | 离屏 `QT_SCALE_FACTOR=2` 合成 |
| UI-22 | 工具栏摆放：选区贴屏幕上/下边、选区极小时与任务栏遮挡 | `placeToolbar` 纯函数已有用例（几何），但真机上从未看过观感 | 工具栏不压住选区读不到的位置、不缺角 | 钉样台按退化选区出图 |
| UI-23 | 行内文字框（`annotationTextEditor`）高度算式与 QSS padding 联动回归 | `resizeTextEditorToContent` 与 `#annotationTextEditor` 注释互相点名 | 多行文字无半行裁切 | 标注窗出图（2/3/5 行） |
| UI-24 | 撤销/重做功不可用时的禁用态是否「看起来可点」 | QSS `QToolButton:disabled` 存在 | 禁用态无饱和色残留 | 出图 + 像素抽查 |
| UI-25 | 「保持按下态」的开关（圆角/双向箭头/描边…）勾选态在深色档是否明显 | `:checked` 三态在 QSS | 亮/暗档都能一眼看出开/关 | 三档出图对比 |
| UI-26 | 工具切换后工具相关按钮的可用状态（不适用时是灰是藏）无定义 | 现状全常显（B 已查实） | 与 B 的修法一并定案 | 随 B 走 |

### 组 3：截图遮罩与取色器

| 编号 | 假设 | 线索 | 判定标准 | 取证方式 |
| --- | --- | --- | --- | --- |
| UI-30 | 遮罩读数条字号 `kHintFontSize=12` 若按像素用则不随 DPI 缩放，高 DPI 下读数变小 | `DesignTokens.h` 里该值是「字号」（12/13/…阶梯），需查 `CaptureOverlay` 的用法是 pt 还是 px | 合成 dpr 1.5/2 下读数与控件字号比例不变 | 读代码 + 合成出图 |
| UI-31 | 放大镜（截图）在 100/150% 下的观感与信息条尺寸 | `kMagnifier*` 常量 | 合成下不糊、不超框 | 合成出图 |
| UI-32 | 选框 2px 边框与 8px 抓点在高 DPI 合成下的形状 | `kSelectionBorderWidth`/`kSelectionGripEdge` | 抓点可见可抓、边框不虚 | 合成出图 |
| UI-33 | 遮罩上「怎么确认/取消」的提示完备性（新用户视角） | 只有读数条；确认靠 Enter/双击 | 首次使用者无需文档可知确认/取消 | 实机走查 + 文案核 |

### 组 4：浮窗族

| 编号 | 假设 | 线索 | 判定标准 | 取证方式 |
| --- | --- | --- | --- | --- |
| UI-40 | 贴图窗悬停出边框的闪烁、旋转后的外接框 | `PinWindow` 悬停画边框；旋转外接框另算 | 无闪烁、无残影 | 出图（悬停态）+ 实机走查 |
| UI-41 | OCR 结果窗预览最小尺寸在 720p 屏上是否过大 | `kPreviewMinimumWidth/Height` | 720p（合成小屏）下窗口不被顶出屏 | 合成小屏出图 |
| UI-42 | 录屏 HUD 时长读数（Metric 等宽）与按钮可用态 | `RecordingHud` | 秒跳不抖宽、按状态禁/启正确 | 出图 + 状态矩阵出图 |
| UI-43 | Toast 位置（多屏/光标屏）与停留时长观感 | `Toast` 固定毫秒 | 单屏两次采样一致 | 出图（三类各一） |
| UI-44 | 托盘菜单与「更多」菜单的图标、条目高度、禁用项 | `TrayController`/`visibleTrayMenuEntries` | 两处同源（已有一条「只有菜单里有」的判据） | 实机走查截图 |
| UI-45 | 贴图组管理窗（`PinGroupManagerWindow`）行高与按钮 | 自定义窗 | 与主窗观感一致 | 出图 |

### 组 5：主题 × DPI 矩阵

| 编号 | 假设 | 线索 | 判定标准 | 取证方式 |
| --- | --- | --- | --- | --- |
| UI-50 | 三档主题 × 各浮窗的对照从没整体看过（样式说明只要求看四处） | `UiStyleGuide.md` §3 | 每档每面：文字可读、禁用态可见、边框不糊 | 钉样台三档全出图，人眼过一遍 |
| UI-51 | dpr 1.25/1.5（非整数）下 1px 边框与字形 hinting | 样式说明 §4 自认「没在真面板上看过」 | 合成下 1px 不消失、文字不糊成一团 | 离屏合成出图（结论标「未上真面板」） |
| UI-52 | 深色档原生件（勾、微调箭头、下拉箭头、占位/禁用文字）漏进浅色 | `buildApplicationPalette` 已有映射，但视觉未整体核过 | 深色档无浅色原生件 | 三档出图对比 |
| UI-53 | 高对比度档 2px 描边是否所有 `:focus` 面都吃到 | QSS 三份同构，文本已核 `2PX SOLID` | 逐焦点面合成出图无一漏 | 出图 |

### 组 6：横切

| 编号 | 假设 | 线索 | 判定标准 | 取证方式 |
| --- | --- | --- | --- | --- |
| UI-60 | 文案一致性：按钮「识别文字」vs 页名「文字识别」；省略号「…」风格；中英混排空格 | 全量扫文案（Languages/翻译目录 + 字面量） | 同类动作同名、标点同风格 | 全文 grep 对照 |
| UI-61 | 焦点圈与 Tab 序在各窗的完备性 | QSS `:focus` 已有；Tab 序未验 | 每窗 Tab 一遍焦点圈不丢 | 实机走查（UIA 可点不可输的约束下） |
| UI-62 | 间距/圆角阶梯一致性（按钮 8px 圆角 vs 卡片 12px；4/8/12/16） | QSS 与令牌 | 同一元素族同值 | QSS 文本比对 |
| UI-63 | 无障碍名覆盖（图标按钮、列表项 `AccessibleTextRole`） | 多数已有；历史列表项需核 | 每个可操作项有名 | 源码扫 + UIA 树抽查 |
| UI-64 | 首启/空态（历史空、托盘装不上、Linux 降级灰行）观感 | 降级逻辑已有 | 空态不出现「空白方块」「假读数」 | 造态出图 |
| UI-65 | 窗口位置记忆与多屏还原（主窗、贴图回位） | 贴图有「截哪儿贴哪儿」；主窗位置未提 | 重启后位置合理 | 实机走查（两次启动） |

## 四、执行顺序与产出

- [ ] **P0 取证设施**：钉样台（浮窗出图 + 尺寸读数）+ 量尺脚本 + 主窗口两档尺寸。
- [ ] **P1 直报两条定案**：A 出「左对齐现状 vs 右对齐」对照 PNG 定案并修；B 出「按工具收敛 vs 两行」对照 PNG 定案并修。两条修完各落用例。
- [ ] **P2 浮窗逐面**：组 2/3/4 按表逐条出图并判定。
- [ ] **P3 设置页与主窗**：组 1 逐条出图/量尺并判定。
- [ ] **P4 矩阵**：组 5（三档主题 × dpr 合成）。
- [ ] **P5 横切与汇总**：组 6 + 全部判定回填成台账。

**台账格式**（每条查实的问题一行）：

```
编号 | 位置 | 证据（PNG 路径 / 读数 / 代码行） | 判定 | 严重度 | 建议方向 | 已落用例(Batch ID / 未)
```

**修复批次纪律**：一个批次 = 一处问题 + 它的用例 + 门禁复跑（`ctest`、`run_samples.py`、文档图重出）；
提交信息按模块名做 scope（不用编号）。

## 五、已有守护面（不重复劳动）

- `ThemeManagerFixture`：对比度、主题像素、换档重绘；`IconProviderFixture`：图标栅格与安全区。
- `Style.SourceConventions`：QSS 形状约定；`TestAnnotationWindow`/`TestAnnotationToolbar`：几何与像素探针。
- `TestMainWindow`、`TestModuleFlow`：主窗与跨模块接线；`UiShotTool` 十张文档图。
- 原则：已有用例钉住的「已成立」事实不重查，只查「没有被钉住的界面事实」——上面清单里的每一条都按这个口径挑过。

## 六、边界与不做项

- 不做 Snipaste 等外部工具的**视觉**对标（对外只做功能缺失对照，另账）。
- 只审不改：本计划的产出是「判定 + 证据 + 方向」，修复是后续批次。
- 真面板高 DPI 观感本机造不出（单屏 dpr 1.0）：合成结论如实标注，不冒充实机验证。
- 系统对话框（文件/颜色选择）与托盘菜单在离屏下拿不到图：记「未验」，留实机走查。

## 附录 A：量尺脚本骨架（Pillow，已验证口径）

```python
from PIL import Image

# 逐行扫：标签段（左缘/右缘）与控件列左缘、缝宽
def scan_rows(path, label_x0=215, label_x1=345, ctrl_x0=346, ctrl_x1=470):
    img = Image.open(path).convert("RGB"); px = img.load()
    rows, cur = [], None
    for y in range(130, img.height - 16):
        dark = any(sum(px[x, y]) < 3 * 200 for x in range(label_x0, label_x1))
        if dark and cur is None:
            cur = y
        elif not dark and cur is not None:
            if y - 1 - cur >= 6:
                rows.append((cur, y - 1))
            cur = None
    out = []
    for y0, y1 in rows:
        ls = next((x for x in range(label_x0, label_x1)
                   if any(sum(px[x, y]) < 3 * 200 for y in range(y0, y1 + 1))), None)
        le = next((x for x in range(label_x1, label_x0, -1)
                   if any(sum(px[x, y]) < 3 * 200 for y in range(y0, y1 + 1))), None)
        cs = next((x for x in range(ctrl_x0, ctrl_x1)
                   if any(abs(px[x, y][0]-255)+abs(px[x, y][1]-255)+abs(px[x, y][2]-255) > 24
                          for y in range(y0, y1 + 1))), None)
        if ls and le and cs:
            out.append((y0, y1, ls, le, cs, cs - le))
    return out  # (行起, 行止, 标签左缘, 标签右缘, 控件左缘, 缝)
```

色块 chip / 按钮节距：在中行找精确色值（如 `#E83E3E`）的列段即 chip；找暗笔画列簇即图标，簇心间距即节距。

## 附录 B：钉样台探针骨架（临时探针纪律：用完撤，`git diff` 自证）

```cpp
// 在 UiShotTool main() 里：先 theme.applyFromSettings()，再建被测控件（与生产同路构造）
GyanisScribe::Annotation::AnnotationToolbar toolbar{settings, icons, nullptr};
toolbar.show();                       // 离屏平台上 show 才会走一遍 doLayout
QCoreApplication::processEvents();
const auto hint = toolbar.sizeHint();
const auto minimum = toolbar.minimumSizeHint();
std::printf("sizeHint=%dx%d minimum=%dx%d\n", hint.width(), hint.height(), minimum.width(), minimum.height());
toolbar.grab().save(QString::fromUtf8((outputDirectory / "annotation-toolbar.png").string().c_str()));
```

注意两条实测口径：离屏平台下顶层 `adjustSize()` 会被 Qt 的 2/3 屏宽规则夹到 `minimumSizeHint`；
`setGeometry` 小于最小尺寸时 Qt 静默保住最小宽——想验「窄屏被切」要用假的 `screenAvailable` 走
`placeToolbar` 纯函数，别指望 `setGeometry`。
