/**
 * @file ShareStyle.h
 * @brief 美化分享的样式值、内置模板与本地模板目录（纯逻辑，不渲染也不碰设置）
 * @author Gyanis
 * @date 2026-11-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace GyanisScribe::Core
{

    /// 卡片留白的可调下限，像素
    inline constexpr int kMinimumSharePadding = 0;

    /// 卡片留白的可调上限，像素：再宽画面就被一圈空白淹没
    inline constexpr int kMaximumSharePadding = 256;

    /// 卡片圆角的可调下限，像素
    inline constexpr int kMinimumShareCornerRadius = 0;

    /// 卡片圆角的可调上限，像素：渲染时还要按卡片短边复夹一次
    inline constexpr int kMaximumShareCornerRadius = 96;

    /// 描边的可调下限，像素
    inline constexpr int kMinimumShareBorderPixels = 0;

    /// 描边的可调上限，像素
    inline constexpr int kMaximumShareBorderPixels = 8;

    /// 投影的可调下限，像素
    inline constexpr int kMinimumShareShadowPixels = 0;

    /// 投影的可调上限，像素：投影是画布留白的一部分，再宽整张成品就发虚
    inline constexpr int kMaximumShareShadowPixels = 64;

    /// 水印不透明度的下限
    inline constexpr int kMinimumShareWatermarkOpacity = 0;

    /// 水印不透明度的上限（0-255）
    inline constexpr int kMaximumShareWatermarkOpacity = 255;

    /// 编辑器标题最多的字符数
    inline constexpr int kMaximumShareEditorTitleLength = 80;

    /// 水印文字最多的字符数
    inline constexpr int kMaximumShareWatermarkLength = 120;

    /// 自定义模板的套数上限
    inline constexpr int kMaximumCustomShareTemplates = 30;

    /// 自定义模板序列化之后的字节上限：这一格随设置一起走，不能无界膨胀
    inline constexpr int kMaximumShareTemplatesJsonBytes = 64 * 1024;

    /// 编辑器标题栏的高度，物理像素；预览时按比例缩放
    inline constexpr int kShareEditorChromeHeight = 44;

    /**
     * @brief 水印贴在卡片内的哪一个下角
     */
    enum class ShareWatermarkCorner
    {
        BottomRight, ///< 右下角
        BottomLeft,  ///< 左下角
    };

    /**
     * @brief 一张美化成品的全部外观参数
     * @details 颜色一律按不透明处理：美化成品总是不透明底，JPG 不需要额外垫白。
     *          缺省值是一套中性的浅底样式，也是旧模板缺字段时的回退值；内置的「留白」
     *          在它之上打开那圈 1px 浅描边。
     */
    struct ShareStyle
    {
        QColor               backgroundTop{QColor{0xF5, 0xF6, 0xF8}};              ///< 背景上沿（渐变起点）
        QColor               backgroundBottom{QColor{0xF5, 0xF6, 0xF8}};           ///< 背景下沿（渐变终点）
        bool                 gradient           = false;                           ///< 背景画不画竖向渐变
        int                  paddingPixels      = 48;                              ///< 卡片与画布边缘之间的留白，像素
        int                  cornerRadiusPixels = 16;                              ///< 卡片圆角半径，像素
        bool                 border             = false;                           ///< 卡片外圈画不画描边
        int                  borderPixels       = 1;                               ///< 描边宽度，像素
        QColor               borderColor{QColor{0xE2, 0xE5, 0xEA}};                ///< 描边色
        bool                 shadow       = true;                                  ///< 卡片外圈画不画投影
        int                  shadowPixels = 32;                                    ///< 投影向外延伸的宽度，像素
        bool                 editorChrome = false;                                 ///< 卡片顶部画不画编辑器标题栏
        QString              editorTitle;                                          ///< 标题栏里的文字；空表示只画那三枚圆点
        QColor               editorChromeColor{QColor{0x1C, 0x1F, 0x26}};          ///< 标题栏底色
        QColor               editorTitleColor{QColor{0x9A, 0xA4, 0xB2}};           ///< 标题文字颜色
        QString              watermarkText;                                        ///< 水印文字；空表示不画
        int                  watermarkOpacity = 128;                               ///< 水印不透明度（0-255）
        ShareWatermarkCorner watermarkCorner  = ShareWatermarkCorner::BottomRight; ///< 水印贴在哪个下角

        friend bool operator==(const ShareStyle &, const ShareStyle &) = default;
    };

    /**
     * @brief 一套命名的样式，内置与自定义共用这一形状
     */
    struct ShareTemplate
    {
        QString    id;    ///< 内置为固定短 id；自定义为 custom- 前缀，删了再加也不撞旧记录
        QString    name;  ///< 下拉里显示的名字
        ShareStyle style; ///< 这套模板的外观参数

        friend bool operator==(const ShareTemplate &, const ShareTemplate &) = default;
    };

    /**
     * @brief 内置的四套模板
     * @return 顺序固定为 light、dark、gradient、code 的副本；名称按当前界面语言查表
     * @details 顺序是界面下拉与文档的共同依据，增删要三处一起改。
     */
    [[nodiscard]] std::vector<ShareTemplate> builtInShareTemplates();

    /**
     * @brief 中性缺省模板：出厂默认样式配固定 id 与「留白」名称
     * @return ShareStyle{} 的字段值；它不是内置「留白」的完整外观（内置那套在它之上打开了描边），
     *         只作为旧模板缺字段与模板缺失时的中性回退
     */
    [[nodiscard]] ShareTemplate defaultShareTemplate();

    /**
     * @brief 判断一个模板 id 是不是内置的
     * @param id 待判定的 id
     * @return 是 light、dark、gradient、code 之一时为真
     */
    [[nodiscard]] bool isBuiltInShareTemplateId(const QString &id);

    /**
     * @brief 校验一份样式是否可用
     * @param style 待校验的样式
     * @return 成功返回空载荷；越界或坏颜色返回 InvalidArgument 与带范围的中文原因
     */
    [[nodiscard]] Base::Expected<void> validateShareStyle(const ShareStyle &style);

    /**
     * @brief 从 JSON 对象读回一份样式
     * @param json 样式对象，键名与 shareStyleToJson 的产出一致
     * @return 成功返回样式；缺键取默认值，越界或类型不对返回 InvalidArgument 与中文原因
     * @note 缺字段是正常情况而不是坏记录：新增一格之后，旧模板必须仍然读得出来。
     */
    [[nodiscard]] Base::Expected<ShareStyle> parseShareStyle(const QJsonObject &json);

    /**
     * @brief 把样式写成 JSON 对象
     * @param style 待序列化的样式
     * @return 键名固定的对象，颜色写成 #rrggbb
     */
    [[nodiscard]] QJsonObject shareStyleToJson(const ShareStyle &style);

    /**
     * @brief 解析设置里存的自定义模板串
     * @param storedJson serializeShareTemplates 产出的文本
     * @return 可用的自定义模板；空串或整串读不懂时是空列表
     * @note 坏条目只丢自己并记一条日志，不挡其余模板：一道坏记录不该让整个下拉空掉。
     */
    [[nodiscard]] std::vector<ShareTemplate> parseShareTemplates(const QString &storedJson);

    /**
     * @brief 把自定义模板序列化成设置里存的那段文本
     * @param templates 自定义模板列表
     * @return 版本 1 的紧凑 JSON 文本
     */
    [[nodiscard]] QString serializeShareTemplates(const std::vector<ShareTemplate> &templates);

    /**
     * @brief 在自定义模板列表末尾追加一套
     * @param templates 现有的自定义模板
     * @param name 用户给的名字，首尾空白会被去掉
     * @param style 这套模板的外观参数
     * @return 成功返回追加后的新列表；名称为空、过长、与现有或内置模板重名、
     *         样式越界、套数满 30 或序列化后超过字节上限时返回 InvalidArgument 与中文原因
     */
    [[nodiscard]] Base::Expected<std::vector<ShareTemplate>> addCustomShareTemplate(const std::vector<ShareTemplate> &templates, const QString &name, const ShareStyle &style);

    /**
     * @brief 从自定义模板列表里删掉一套
     * @param templates 现有的自定义模板
     * @param id 要删的模板 id
     * @return 成功返回删除后的新列表；内置 id 或找不到时返回 InvalidArgument 与中文原因
     */
    [[nodiscard]] Base::Expected<std::vector<ShareTemplate>> removeCustomShareTemplate(const std::vector<ShareTemplate> &templates, const QString &id);

    /**
     * @brief 造一个新的自定义模板 id
     * @return custom- 前缀加一段 UUID 前八位
     */
    [[nodiscard]] QString newCustomShareTemplateId();

} // namespace GyanisScribe::Core
