/**
 * @file ScreenUtils.h
 * @brief 屏幕与选区的几何换算，以及选区文本的存取格式
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/IScreenCaptureSource.h"
#include "Base/Types.h"

#include <QLine>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

class QScreen;

namespace GyanisScribe::Core
{

    /// 圆角截图半径的默认档，逻辑像素：12 格在 100% 与 150% 屏上都看得出「切了个圆」又不吃内容
    inline constexpr int kCaptureCornerRadiusDefault = 12;

    /// 圆角半径下限，逻辑像素：小于 1 就与不切没有区别，那一档由「圆角截图」那个开关负责
    inline constexpr int kCaptureCornerRadiusMinimum = 1;

    /// 圆角半径上限，逻辑像素：再往上切掉的就是内容本身，而不是四个角
    inline constexpr int kCaptureCornerRadiusMaximum = 60;

    /**
     * @brief 校验配置里读出的圆角半径
     * @param logical 配置读出的整数（逻辑像素）
     * @return 成功返回可用的半径；不在 [1,60] 里时返回带范围与补救办法的中文原因
     */
    [[nodiscard]] Base::Expected<int> parseCaptureCornerRadius(int logical);

    /**
     * @brief 把某块屏幕内的逻辑矩形换成同一块屏幕上的物理像素矩形
     * @param logicalRect 该屏幕坐标系下的逻辑矩形（Qt 的高 DPI 坐标）
     * @param devicePixelRatio 该屏的缩放因子，必须大于 0
     * @return 取整后的物理矩形；四边按「起点向下取整、终点向上取整」扩，保证用户框到的边不会被切掉半像素
     * @note 判据是「两套换算互指同一个点」：cropToLogicalRect() 用同一份取整规则，
     *       两边不一致时标注层与最终像素会错位半格。缩放因子非正一律拒，不悄悄按 1 处理。
     */
    [[nodiscard]] Base::Expected<QRect> toPhysicalRect(const QRect &logicalRect, qreal devicePixelRatio);

    /**
     * @brief 从一帧抓屏结果里裁出选区
     * @param source 抓到的整帧（物理像素）
     * @param logicalRegion source 所属屏幕内的逻辑矩形
     * @return 成功返回裁剪后的帧，其 logicalSize 与 devicePixelRatio 同步改好
     * @return 失败返回 ScreenUnavailable，文案指出选区超出哪一条边
     */
    [[nodiscard]] Base::Expected<Base::CapturedImage> cropToLogicalRect(const Base::CapturedImage &source, const QRect &logicalRegion);

    /**
     * @brief 按屏幕名取屏幕
     * @param screenName QScreen::name() 的那个名字
     * @return 找到返回该屏幕指针，所有权仍在 Qt 手里
     * @return 找不到返回空指针：多显示器被拔掉或名字写错时由调用方给出降级提示
     */
    [[nodiscard]] QScreen *screenByName(const QString &screenName);

    /**
     * @brief 把一张抓到的像素图装成帧：补上逻辑尺寸、缩放因子与屏名
     * @param grabbed 抓到的像素（物理像素）
     * @param screen 来源屏幕，可为空（拿不到时缩放因子按 1、屏名留空）
     * @param logicalSize 这一帧对应的逻辑尺寸
     * @return 组装好的帧；图本身统一标成 dpr 1，物理与逻辑的互指只走 devicePixelRatio 一格
     * @details 两份抓帧来源（Qt 与 DXGI）共用这一处：两处各写一份时，缩放因子标错会表现为
     *          「高 DPI 屏上标注与成品错位」，而那种错位只在特定屏上出现，很难在现场看出根因。
     */
    [[nodiscard]] Base::CapturedImage frameFromGrabbedImage(const QImage &grabbed, const QScreen *screen, const QSize &logicalSize);

    /**
     * @brief 取覆盖某个全局逻辑点的屏幕
     * @param globalPoint 全局逻辑坐标
     * @return 命中的屏幕指针；一点正落在两屏接缝上时取声明顺序里的那一块
     * @return 该点不在任何屏幕上（拔屏后的陈旧坐标）时返回空指针
     */
    [[nodiscard]] QScreen *screenAtGlobalPoint(const QPoint &globalPoint);


    /**
     * @brief 从一叠候选矩形里认出压在某个点上方的那一块
     * @param point 待判定的点，与候选同一坐标系
     * @param candidates 候选矩形，按压盖顺序从最上面排到最下面
     * @return 第一个含住这个点的矩形；一个都不含时返回空
     * @details 只取第一个命中：这份顺序就是谁压在谁上面，往后找会把用户看得见的那一块换成被压在底下的一块
     */
    [[nodiscard]] std::optional<QRect> regionAtPoint(const QPoint &point, const std::vector<QRect> &candidates);

    /**
     * @brief 选区提示文本
     * @param logicalRegion 选区的逻辑矩形
     * @return 形如「1024 × 640    (128, 96)」的文本，字号小、只用于遮罩上的读数
     */
    [[nodiscard]] QString formatSelectionHint(const QRect &logicalRegion);

    /**
     * @brief 选区的存取文本形式
     * @param logicalRegion 选区
     * @return 「x,y,width,height」四个十进制数，供写进配置
     */
    [[nodiscard]] QString regionToText(const QRect &logicalRegion);

    /**
     * @brief 解析选区的存取文本
     * @param text 来自配置的文本
     * @return 成功返回选区
     * @return 失败返回 InvalidArgument，文案写明期望的形状与实际收到的内容
     */
    [[nodiscard]] Base::Expected<QRect> regionFromText(std::string_view text);

    /**
     * @brief 把遮罩尺寸框里那一行换成一块选区
     * @param text 框里的文本，认两种写法：「宽x高」（左上角不动）与「x,y,宽,高」（连位置一起指定）
     * @param anchor 只给尺寸时选区的左上角（还没有选区时是这块屏的原点）
     * @param limit 允许的范围，即这块屏的矩形
     * @return 成功返回夹在 limit 内的选区；两种写法都不认时返回空
     * @details 后一种与配置里那条「上次选区」的记录同一套写法（`regionFromText`），
     *          不必为了「把位置也敲进去」再造第三种文本格式。
     */
    [[nodiscard]] std::optional<QRect> regionFromSizeText(const QString &text, const QPoint &anchor, const QRect &limit);

    /**
     * @brief 遮罩上那些数字用哪种像素写
     * @details 高 DPI 屏上这两组数不一样：一块 1000 逻辑像素宽的选区在 1.5 倍屏上是 1500 物理像素。
     *          选区自己那一层永远存逻辑像素（配置里的记录、抓取与裁剪都按它走），这一档只管**给人看的那几处**。
     */
    enum class SizeUnit
    {
        Logical,  ///< 逻辑像素（dip）：与遮罩上其余一切同一套坐标
        Physical, ///< 物理像素（px）：与成品图的宽高同一把尺子
    };

    /**
     * @brief 单位档的界面名
     * @param unit 哪一档
     * @return 设置页与提示里的那两个字
     */
    [[nodiscard]] QString sizeUnitLabel(SizeUnit unit);

    /**
     * @brief 单位档存进配置的标记
     * @param unit 哪一档
     * @return 「dip」或「px」
     */
    [[nodiscard]] std::string_view sizeUnitToken(SizeUnit unit);

    /**
     * @brief 把配置里那串标记换成单位档
     * @param token 配置里的值
     * @param complaint 出参：读坏时写进原因，没读坏时留空
     * @return 认得的档原样交回；认不出的落回逻辑像素
     * @details 落回逻辑而不是夹成「看着最像的那一档」：把「pix」读成物理像素是猜用户的意思，
     *          而猜错的数与填的数差着一个缩放倍率，比回到默认档更难查。
     */
    [[nodiscard]] SizeUnit sizeUnitOrDefault(std::string_view token, std::string &complaint);

    /**
     * @brief 把一块逻辑矩形换成某一档的那一块
     * @param logicalRegion 选区的逻辑矩形
     * @param devicePixelRatio 这块屏的缩放因子
     * @param unit 按哪一档量
     * @return 逻辑档原样交回；物理档是抓帧真正拿到的那一片
     * @details 起点与尺寸成对换算，只换一半会把读数摆到别处去。物理档复用 `toPhysicalRect`：
     *          它按「起点向下取整、终点向上取整」算，交回的就是成品图的真实宽高，读数与产物不会分家。
     *          缩放因子坏了时按逻辑那一份交回——这一条只服务读数；抓帧交付那条链自己会报错并给出路。
     */
    [[nodiscard]] QRect rectInUnit(const QRect &logicalRegion, qreal devicePixelRatio, SizeUnit unit);

    /**
     * @brief 把按某一档填出来的矩形换回逻辑那一份
     * @param unitRegion 按那一档量出来的矩形
     * @param devicePixelRatio 这块屏的缩放因子
     * @param unit 填的是哪一档
     * @return 遮罩那一层用得上的逻辑矩形（宽高至少一格）
     * @details 换算按四舍五入：分数倍屏上不是每个物理数都落得回整格逻辑数，
     *          换回去之后读数会显示真正拿到的那一格，不会把「敲进去的」与「拿到的」写成两个数。
     */
    [[nodiscard]] QRect rectFromUnit(const QRect &unitRegion, qreal devicePixelRatio, SizeUnit unit);

    /**
     * @brief 尺寸框里那一行按指定的单位换成一块选区
     * @param text 框里的文本，认两种写法：「宽x高」与「x,y,宽,高」
     * @param anchor 只给尺寸时选区的左上角，逻辑坐标
     * @param limit 允许的范围，逻辑坐标
     * @param devicePixelRatio 这块屏的缩放因子
     * @param unit 敲的那组数按哪一档量
     * @return 成功返回夹在 limit 内的逻辑选区；两种写法都不认时返回空
     * @details 读数、复制与输入框三处共用这一档：输入按另一档解释的话，
     *          刚看到的数敲回去会得到另一块选区。
     */
    [[nodiscard]] std::optional<QRect> regionFromSizeText(const QString &text, const QPoint &anchor, const QRect &limit, qreal devicePixelRatio, SizeUnit unit);

    /**
     * @brief 把一张图按给定半径切圆四角
     * @param image 源图
     * @param radiusPhysical 圆角半径，物理像素；非正时原样交回
     * @return 四角切圆后的 ARGB 图：圆角之外是透明像素，边与内里一个像素不动
     * @details 「圆角矩形截图」的成品加工：走掩膜而不是裁剪边，圆弧上才不会有台阶。
     *          半径由调用方按缩放因子换好再送进来，这里不做 dpr 换算。
     */
    [[nodiscard]] QImage roundedCornerImage(const QImage &image, int radiusPhysical);

    /**
     * @brief 三分网格的两竖两横，摆在选区里当构图参考线
     * @param region 选区（该屏内的逻辑坐标）
     * @return 两条竖线与两条横线：竖在宽的三分之一与三分之二处、横在高的同比例处
     * @note 取整按整数除法：极小的选区里两条线可能与边重合，画出来也只是叠在框上，
     *       不做「小于多少就不画」的分支——那种阈值很快会变成第二份规格
     */
    [[nodiscard]] std::array<QLine, 4> thirdsGridLinesFor(const QRect &region);

    /**
     * @brief 按住了 Shift 时把选区的另一个端点锁成正方形
     * @param anchor 起手那个端点
     * @param current 光标所在端点
     * @param locked 是否按住了 Shift
     * @param limit 允许的范围（通常是这块屏）
     * @return 该用的端点；没上锁时原样交回 current
     * @details 正方形取两个方向里大的那一边：用户要的是「至少盖住我拖过的那一截」，
     *          取小的会让选区在视觉上往回缩。夹取之后再按两轴里更小的偏移量对称收一次，
     *          贴到屏边时仍然是正方形——不然「锁了正方形」在边上会悄悄失效。
     */
    [[nodiscard]] QPoint squareLockedPoint(const QPoint &anchor, const QPoint &current, bool locked, const QRect &limit);

    /**
     * @brief 锁着某一档长宽比时，把选区的另一个端点收到这条比例上
     * @param anchor 起手那个端点
     * @param current 光标所在端点
     * @param ratio 锁住的长宽比（已约分，如 16:9）；任一维非正时原样交回 current
     * @param limit 允许的范围（通常是这块屏）
     * @return 该用的端点：从 anchor 朝光标那一侧，按比例铺一块尽量盖住光标的矩形
     * @details 与方框版本同一套做法：取「两轴里要求更大的那一边」定尺度（少盖住光标会让人觉得锁反了），
     *          贴边被夹之后按能落下的最小尺度收一次，比例不因贴边失效。
     */
    [[nodiscard]] QPoint ratioLockedPoint(const QPoint &anchor, const QPoint &current, const QSize &ratio, const QRect &limit);

    /**
     * @brief 把长宽比写成存取文本
     * @param ratio 长宽比；空或非正时返回空串
     * @return 约分后的「宽:高」，如「16:9」
     */
    [[nodiscard]] QString aspectRatioToText(const QSize &ratio);

    /**
     * @brief 解析长宽比的存取文本
     * @param text 形如「16:9」；两侧非正数、缺冒号或多余的尾巴都算解析不出
     * @return 成功返回约分后的长宽比；解析不出时为空
     */
    [[nodiscard]] std::optional<QSize> aspectRatioFromText(const QString &text);

    /**
     * @brief 把选区整体挪若干格
     * @param region 当前选区
     * @param step 位移量，一格是 (1,0)、(0,-1) 这种
     * @param limit 允许的范围（通常是这块屏），矩形从它的左上角起算
     * @return 挪好后的选区；宽高不变，越出 limit 的那一边被夹回来
     * @details 夹住而不是绕到对面：方向键按过头时用户要的是「到头了」，不是选区跳到屏的另一角。
     */
    [[nodiscard]] QRect nudgeRegion(const QRect &region, const QPoint &step, const QRect &limit);

    /**
     * @brief 把选区的右下角挪若干格，顶边不动
     * @param region 当前选区
     * @param step 右下角的位移量
     * @param limit 允许的范围
     * @return 改好后的选区；至少留一格宽与一格高，越出 limit 的那一边被夹回来
     * @details 下限是一格而不是零：缩成 0 的选区在遮罩上「看着还在」，交出去却是一张空图。
     */
    [[nodiscard]] QRect resizeRegion(const QRect &region, const QPoint &step, const QRect &limit);

    /**
     * @brief 把选区在 step 指的那一侧那条边外扩或内收若干格，对边不动
     * @param region 当前选区
     * @param step 要动哪条边（方向键翻译来的单位向量，两轴可以都有）
     * @param shrink true 表示把那条边往里收（选区变小）
     * @param limit 允许的范围
     * @return 改好后的选区；宽或高已经是一格时那一轴不再收，越出 limit 的边被夹回来
     * @details 与 `resizeRegion`（只动右下角）分开的理由：已经框好后要补的那一像素常在起始边那一侧
     *          （图标左边缘差一格），只动右下角会把差的那格补到对面去，越改越偏。
     */
    [[nodiscard]] QRect stretchRegionByEdge(const QRect &region, const QPoint &step, bool shrink, const QRect &limit);

    /**
     * @brief 选区上可以抓住的那个点
     * @details Inside 是整块平移，None 表示这一点不该动选区。
     *          被用例钉住的枚举新成员只能追加在末尾。
     */
    enum class SelectionGrip
    {
        None,        ///< 没抓住任何东西
        Inside,      ///< 选区内部，整块平移
        TopLeft,     ///< 左上角
        Top,         ///< 上边
        TopRight,    ///< 右上角
        Right,       ///< 右边
        BottomRight, ///< 右下角
        Bottom,      ///< 下边
        BottomLeft,  ///< 左下角
        Left,        ///< 左边
    };

    /// 抓住一个边点允许的容差，逻辑像素：再小就要用户去对准一条一像素宽的线，鼠标做不到
    inline constexpr int kSelectionGripTolerance = 8;

    /**
     * @brief 认一个位置压在选区的哪个点上
     * @param region 选区
     * @param point 待判定的位置，与选区同一坐标系
     * @param tolerance 边点的容差
     * @return 八个边点里最近的那一个（不超过容差）；否则在选区内给 Inside，再否则 None
     * @details 先认边点再认内部：一个点既贴着左边又落在区域内时，用户要的是拖这条边，
     *          认成 Inside 会把整块推走。
     */
    [[nodiscard]] SelectionGrip gripAt(const QRect &region, const QPoint &point, int tolerance = kSelectionGripTolerance);

    /**
     * @brief 按住某个点把选区拖成什么样
     * @param region 当前选区
     * @param grip 抓住的点
     * @param delta 这一帧的位移量，逻辑像素
     * @param limit 允许的范围
     * @return 拖好后的选区；None 原样返回，其余情况不许越过对边、也不许越出 limit
     * @details 对边钉住不动，拖过头时最多收成一格：允许越过就把选区翻了个面，
     *          交出去的矩形会少掉用户本来要的那一条边。
     */
    [[nodiscard]] QRect dragRegionByGrip(const QRect &region, SelectionGrip grip, const QPoint &delta, const QRect &limit);

    /**
     * @brief 某个抓点对应的鼠标形状
     * @param grip 抓住的点
     * @return 该用的光标：边点给对应方向的伸缩箭，Inside 给四向移动，None 给十字
     */
    [[nodiscard]] Qt::CursorShape cursorForGrip(SelectionGrip grip);

    /**
     * @brief 指针形状该落在帧内的哪一个物理矩形
     * @param sprite 指针形状、热点与屏内位置
     * @param devicePixelRatio 该帧的缩放因子，必须大于 0
     * @return 帧内坐标下的落点矩形，可能整块或部分在帧外；缩放因子非正时退回按 1 算，
     *         不返回空矩形冒充「不在屏上」
     * @details 只有位置受缩放因子影响：形状本身已是系统按该屏 DPI 挑出来的物理像素，
     *          再乘一次就会把指针放大一圈。落点 = 屏内逻辑位置乘因子，再按热点回退。
     */
    [[nodiscard]] QRect cursorRectInPixels(const CursorSprite &sprite, qreal devicePixelRatio);

    /**
     * @brief 解析「不参与窗口检测的程序」那一格
     * @param text 界面上填的原文，逗号分隔
     * @return 去空白、转小写、去重之后的名单；空文本给空名单（谁都不排除）
     * @details 转小写在这一层做掉：Windows 的文件名不区分大小写，用户写 `Chrome.exe` 也该对上 `chrome.exe`。
     */
    [[nodiscard]] QStringList parseProcessExcludes(const QString &text);

    /**
     * @brief 这个进程名在不在排除清单里
     * @param processName 平台给出的可执行文件名（不含路径）
     * @param excludes `parseProcessExcludes` 处理过的名单
     * @return 名单里有它时为真
     * @details 名字取不到（空串）时一律为假：把「问不出进程名」当成「用户点名要排除的那个」，
     *          会让悬停识别整片失效，而用户只觉得「这功能坏了」。
     */
    [[nodiscard]] bool isExcludedProcess(const QString &processName, const QStringList &excludes);

    /**
     * @brief 把指针画进这一帧，就地改 frame.image
     * @param frame 抓到的整帧（物理像素，缩放因子按 frame.devicePixelRatio 参与落点换算）
     * @param sprite 指针形状与落点
     * @details 算好落点交给 QPainter 做源-over 混合：部分透明与贴屏边裁切都由它兜，
     *          自己写逐像素回绕会在高缩放屏上把指针画糊。
     */
    void drawCursorInto(Base::CapturedImage &frame, const CursorSprite &sprite);

} // namespace GyanisScribe::Core
