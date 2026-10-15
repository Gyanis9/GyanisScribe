/**
 * @file ClipboardPin.h
 * @brief 剪贴板内容转成可贴的那张图：图像直接用，纯文本与颜色各自渲染成卡片
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPoint>
#include <QString>
#include <QUrl>
#include <QStringList>

#include <vector>

class QMimeData;

namespace GyanisScribe::Pin
{
    class PinManager;
} // namespace GyanisScribe::Pin

namespace GyanisScribe::App
{

    /// 文本卡片：内边距（逻辑像素）与整卡的宽度上限；宽度封顶之后按词折行
    inline constexpr int       kTextCardPadding           = 12;
    inline constexpr int       kTextCardMaximumWidth      = 640;
    inline constexpr int       kTextCardMinimumWidth      = 120;
    inline constexpr qsizetype kMaximumTextCardCharacters = 2000; ///< 超过这个字数的文本不渲染：整卡会长到没法看

    /// 识别颜色文本时的长度上限：一行、短、且必须以 # / rgb( / hsl( 开头才算，别把「red」这种词当成颜色
    inline constexpr int kMaximumColorTextLength = 32;

    /**
     * @brief 把剪贴板里的内容转成可贴的那张图
     * @param data 剪贴板的 mime 载荷，可为 nullptr（无人拷贝过时有的平台就返回空指针）
     * @param font 渲染文本卡片用的字体（生产传应用字体，用例传固定字体）
     * @return 成功返回图像：图像原样交回，纯文本与颜色各自渲染成卡片
     * @details 优先序：图像 → 颜色 → 纯文本。颜色只认 `#`、`rgb(`、`hsl(` 开头的短单行，
     *          免得把「red」这类普通单词渲染成色卡；HTML、链接与文件路径不在这条里冒充。
     */
    [[nodiscard]] Core::Expected<QImage> pinContentFrom(const QMimeData *data, const QFont &font);

    /**
     * @brief 把一段纯文本渲染成一张卡片
     * @param text 文本（调用方已去掉首尾空白）
     * @param font 基准字体，字号按卡片那一档统一
     * @return 白底深字、细边、带内边距的图；宽度封顶之后按词折行
     * @details 白底是刻意的：卡片是独立成品，不跟着主题走——深色主题下渲染成深底，
     *          贴到浅色桌面上就看不清了。
     */
    [[nodiscard]] QImage renderTextCard(const QString &text, const QFont &font);

    /**
     * @brief 这一段载荷是不是「一种颜色写法」，是就交出它
     * @param data 剪贴板的 mime 载荷，可为 nullptr
     * @return 认出颜色时返回那个颜色；不是颜色文本时返回空
     * @details 与 `pinContentFrom` 里那条判据同一个出处：贴图创建方要拿它在贴的时候记下
     *          「这张色卡原来是哪个色」，供右键「改这一张的颜色…」做初值。两处各写一份识别，
     *          就会出现「渲染成色卡了但改色找不到初值」这种半接上的形状。
     */
    [[nodiscard]] std::optional<QColor> colorCardColorOf(const QMimeData *data);

    /**
     * @brief 这一段载荷里那条「网页上的图」的地址
     * @param data 拖放或剪贴板的 mime 载荷，可为 nullptr
     * @return 恰好一条 http/https 地址时返回它；带别的件、或不是那种地址时返回空
     * @details 只认「恰好一条」：拖进来好几条链接时「先取哪一张」没有合理答案，交给原来那条路（渲染成文本卡）。
     *          本地文件不在这里——它有自己那条读文件的转换。
     */
    [[nodiscard]] std::optional<QUrl> imageUrlOf(const QMimeData *data);

    /**
     * @brief 一份可以贴出去的内容
     */
    struct PinContent
    {
        QImage                image;          ///< 要贴的像素
        QString               animatedSource; ///< 动图来源文件；空表示这一份是静图
        std::optional<QColor> colorCard;      ///< 这一份是从一段颜色写法渲染出来时，那个颜色本身；空表示不是色卡
    };

    /**
     * @brief 拖放载荷渲染出来的全部可贴内容
     */
    struct DropContent
    {
        std::vector<PinContent> contents;      ///< 可贴的若干份，按拖进来的顺序
        QStringList             rejectedNames; ///< 没能当成图片的文件名（不是图片或读不出）
    };

    /**
     * @brief 把拖放载荷转成可贴的内容
     * @param data 拖放的 mime 载荷，可为 nullptr
     * @param font 渲染卡片用的字体（生产传应用字体，用例传固定字体）
     * @return 成功返回可贴的若干份；一件都贴不出来时返回带原因的错误
     * @details 有文件就按文件走：图片读成像素（GIF 这类连来源路径一起带走，贴上去会接着播），
     *          链接渲染成文本卡片；没有文件时退回剪贴板那套单件内容（图像 / 颜色卡 / 文本卡）。
     *          不是图片的文件不拦整次拖放：能贴的照贴，名字记进 rejectedNames 由调用方提示。
     */
    [[nodiscard]] Core::Expected<DropContent> dropContentFrom(const QMimeData *data, const QFont &font);

    /**
     * @brief 一次拖放的贴图结果
     */
    struct DropPinOutcome
    {
        int         pinnedCount = 0; ///< 成功贴出的张数
        QStringList rejectedNames;   ///< 没能当成图片贴出的文件名
    };

    /**
     * @brief 把拖进来的东西贴成若干张新图
     * @param pins 贴图管理器
     * @param data 拖放载荷
     * @param globalTopLeft 落点，第一张新贴图的左上角；多张依次向右下错开
     * @param font 渲染卡片用的字体
     * @return 成功返回贴出的张数与没贴成的文件名；一件都贴不出来时返回原因
     * @details 与「把剪贴板里的内容贴到屏幕上」共用同一套内容渲染，两处不会将来只改一处。
     */
    [[nodiscard]] Core::Expected<DropPinOutcome> pinDroppedContent(Pin::PinManager &pins, const QMimeData *data, const QPoint &globalTopLeft, const QFont &font);

} // namespace GyanisScribe::App
