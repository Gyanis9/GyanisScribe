/**
 * @file IconProvider.h
 * @brief 图标供给器：按名字取原创 SVG，按主题上色，按 DPI 光栅化
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

#include <string_view>

namespace GyanisScribe::Ui
{

    /**
     * @brief 图标尺寸阶梯
     * @details 枚举值就是逻辑像素数，因此新增一档不需要在第二处登记尺寸表；
     *          阶梯的四个取值由需求第六节钉死（16 / 20 / 24 / 32）。
     */
    enum class IconSize
    {
        Small      = 16, ///< 托盘、菜单项行内图标
        Medium     = 20, ///< 紧凑工具栏按钮
        Large      = 24, ///< 工具栏按钮与对话框默认档
        ExtraLarge = 32  ///< 关于页、空状态提示
    };

    /**
     * @brief 全应用唯一的图标出口
     * @details SVG 源码里的描边色写成 currentColor，而 Qt 的 SVG 渲染器不解析这个 CSS 关键字，
     *          所以本类在载入后把它替换成主题色再渲染——深浅两套主题因此共用同一批文件。
     */
    class IconProvider
    {
    public:
        /**
         * @brief 构造供给器
         * @details 构造时显式注册 qrc：AUTORCC 生成的那份初始化代码躺在另一个目标文件里，
         *          没有任何符号引用它时链接器会整份丢掉，于是「仓库里有 56 个 SVG，运行时 :/icons 却是空的」。
         *          这条不是推测：摘掉显式注册后重编，四枚图标用例当场变红。
         */
        IconProvider();

        /**
         * @brief 设置主题描边色
         * @param tintColor 替换 currentColor 的目标色，来自主题的图标语义色
         */
        void setTintColor(const QColor &tintColor);

        /**
         * @brief 取当前描边色
         * @return 已显式设置过就用设置值，否则回落到应用调色板的文本色
         * @details 回落项让没接主题的场合（单元测试、自检）也能画出可见的图标，而不是回退成黑色硬编码。
         */
        [[nodiscard]] QColor tintColor() const;

        /**
         * @brief 取一枚多尺寸图标
         * @param name SVG 文件名去掉扩展名，例如 "Capture"
         * @return 含 16/20/24/32 四档且每档带各屏幕 DPR 副本的图标；名字不存在时返回空图标
         * @note 名字拼错不会崩也不会静默换成别的图：返回空 QIcon 并记一条含可用名单线索的错误日志。
         */
        [[nodiscard]] QIcon icon(std::string_view name) const;

        /**
         * @brief 取单档像素图
         * @param name SVG 文件名去掉扩展名，例如 "Tray"
         * @param size 尺寸阶梯档位
         * @param devicePixelRatio 目标屏幕的缩放比，取 1 到 3 之外的值按 3 封顶
         * @return 已写好 devicePixelRatio 的位图；名字不存在时返回空位图
         */
        [[nodiscard]] QPixmap pixmap(std::string_view name, IconSize size, qreal devicePixelRatio = 1.0) const;

        /**
         * @brief 列出资源里真实存在的图标名
         * @return 按字典序排列、不带 .svg 后缀的名字表
         * @details 供设置页、自检与测试用：调用方不需要知道 :/icons 这个前缀，路径规则只在本类里出现一次。
         */
        [[nodiscard]] static QStringList availableIconNames();

    private:
        /**
         * @brief 读出一枚 SVG 源码并把 currentColor 换成主题色
         * @param name SVG 文件名去掉扩展名
         * @param tint 目标描边色
         * @return 可直接交给渲染器的 SVG 文本；资源缺失时返回空串
         */
        [[nodiscard]] static QString loadTintedSource(std::string_view name, const QColor &tint);

        QColor m_tintColor; ///< 显式设置的主题色，无效表示尚未接主题

        // ponytail: 每次调用都重画，没有缓存。SVG 只有五十几枚、调用点都在界面搭建期，
        //         真出现逐帧取图标（比如动画）时在类里加一张 QHash<(名字,尺寸,色), QPixmap> 就够。
    };

} // namespace GyanisScribe::Ui
