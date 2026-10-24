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
#include <QHash>
#include <QIcon>
#include <QPixmap>

#include <string_view>

class QAbstractButton;
class QWidget;

namespace GyanisScribe::Gui
{

    /**
     * @brief 图标尺寸阶梯
     * @details 枚举值就是逻辑像素数，因此新增一档不需要在第二处登记尺寸表；
     *          16/20/24/32 是控件真实请求的四档，96 是历史列表类别图标，其余是家族的大尺寸输出。
     */
    enum class IconSize
    {
        Small      = 16,  ///< 菜单项与托盘行内图标
        Medium     = 20,  ///< 导航、动作条、标注工具栏
        Large      = 24,  ///< 对话框默认档
        ExtraLarge = 32,  ///< 较大的按钮与空状态
        Huge       = 48,  ///< 大尺寸输出
        Display    = 64,  ///< 展示位
        History    = 96,  ///< 历史列表类别图标
        Maximum    = 128  ///< 最大界面图标档
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
         *          没有任何符号引用它时链接器会整份丢掉，于是「仓库里一批 SVG 全在，运行时 :/icons 却是空的」。
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
         * @brief 取品牌图标的多尺寸版本
         * @return 由 /brand 下 8 个 PNG 组成的 QIcon，固定色、不随主题上色
         */
        [[nodiscard]] QIcon brandIcon() const;

        /**
         * @brief 取品牌位图
         * @param logicalSize 逻辑边长
         * @param devicePixelRatio 目标屏幕缩放比，夹在 1 到 3
         * @return 物理边长最接近 logicalSize × ratio 的原生 PNG；读不出时返回空位图
         * @details 品牌图不经过 currentColor 替换，也不读取 m_tintColor。
         */
        [[nodiscard]] QPixmap brandPixmap(int logicalSize, qreal devicePixelRatio) const;

        /**
         * @brief 给按钮挂一枚按名字取的图标，并把这个名字记在控件上
         * @param button 目标按钮
         * @param name SVG 文件名去掉扩展名，例如 "Export"
         * @details 别直接 setIcon(icon(name))：QIcon 是取色那一刻的快照，换主题之后已发出去的
         *          图标仍留着旧描边色（深色档下等于看不见）。记下名字才谈得上重取，见 refreshIcons。
         */
        void applyIcon(QAbstractButton &button, std::string_view name) const;

        /**
         * @brief 把 root 子树里由 applyIcon 挂过的按钮按当前描边色重取一次图标
         * @param root 起点控件，通常是某个顶层窗口
         * @details 只动带名字属性的按钮，没走过 applyIcon 的按钮不碰——它们的图标可能是自绘的
         *          （标注工具栏的色块就是），重取会画错。
         */
        void refreshIcons(QWidget &root) const;

        /**
         * @brief 取单档像素图
         * @param name SVG 文件名去掉扩展名，例如 "Tray"
         * @param size 尺寸阶梯档位
         * @param devicePixelRatio 目标屏幕的缩放比，取 1 到 3 之外的值按 3 封顶
         * @return 已写好 devicePixelRatio 的位图；名字不存在时返回空位图
         */
        [[nodiscard]] QPixmap pixmap(std::string_view name, IconSize size, qreal devicePixelRatio = 1.0) const;

        /**
         * @brief 按显式描边色取单档像素图
         * @param name SVG 文件名去掉扩展名，例如 "Tray"
         * @param size 尺寸阶梯档位
         * @param devicePixelRatio 目标屏幕的缩放比，取 1 到 3 之外的值按 3 封顶
         * @param tint 本次要的描边色；无效时回落到 tintColor()
         * @return 已写好 devicePixelRatio 的位图；名字不存在时返回空位图
         * @note 描边色进缓存键：同一枚图标按两种颜色各存一格，不会把上一色的那份当新的发出去。
         */
        [[nodiscard]] QPixmap pixmap(std::string_view name, IconSize size, qreal devicePixelRatio, const QColor &tint) const;

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

        /**
         * @brief 光栅化过的位图，键是「名字 # 档位 # 描边色 # 缩放比」四个分量拼出来的
         * @details 取图标的不止界面搭建那一次：录屏 HUD 每存一帧就按状态重挂暂停按钮的图标，
         *          而一枚图标是八档尺寸 × 若干缩放比的光栅化，每帧重画一遍纯属白干。
         * @note 描边色必须在键里：少了它，换主题之后旧色那一份会被当成新图标发出去，
         *       正是 applyIcon() 的 @details 明令禁止的那种错。缩放比也要在键里（用夹住之后的值），
         *       否则换到另一块 DPI 的屏上还会拿到低缩放的那一份。
         *       条目数有界：名字只有 src/Gui/Resources/Icons 里那几十枚，档位四格、缩放比封顶 3、主题三档，
         *       所以不做淘汰。空位图不入表——那是错误出口，下一次调用还要重新报出原因。
         */
        mutable QHash<QString, QPixmap> m_pixmapCache; ///< 光栅化结果，键的四个分量见 @details
    };

} // namespace GyanisScribe::Gui
