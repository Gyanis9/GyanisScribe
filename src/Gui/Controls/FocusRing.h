/**
 * @file FocusRing.h
 * @brief 输入面的焦点环覆盖层：在宿主控件坐标系里画双环或单环
 * @author Gyanis
 * @date 2026-10-24
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Theme/DesignTokens.h"

#include <QEvent>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <optional>

namespace GyanisScribe::Gui
{

    /// 焦点环的两种形状：输入面用外圈淡环构成双环，按钮与勾选用单环
    enum class FocusRingStyle
    {
        Double, ///< 输入面的双环：贴边内环加一圈半透明外环
        Single, ///< 按钮与勾选的单环：只画贴边那一圈
    };

    /**
     * @brief 画在宿主控件上的焦点环
     * @details 自身透明、不吃鼠标、不参与焦点；几何跟着目标控件走，目标不可见时自动收起。
     *          覆盖层不占布局，因此 6px 外扩不会挤动任何控件。
     *          宿主是能装下 6px 外扩的最近祖先，通常是页卡片或窗口根，而不是目标控件的直接父控件：
     *          零边距的行容器与目标等大，挂在它上面会被 Qt 裁掉外圈。
     */
    class FocusRingOverlay final : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造覆盖层并挂到宿主控件上
         * @param target 焦点环围绕的控件，必须已有父控件
         * @param style 双环或单环
         * @param indicatorOnly 为 true 时只圈复选框的指示器，而不是整行
         * @param palette 当前主题的语义色
         * @param parent 能装下 6px 外扩的祖先控件，覆盖层作为它的子控件压在最上层
         */
        FocusRingOverlay(QWidget *target, FocusRingStyle style, bool indicatorOnly, ThemePalette palette, QWidget *parent);

        /**
         * @brief 换一套主题色并立即重绘
         * @param palette 当前主题的语义色
         */
        void setPalette(const ThemePalette &palette);

        /**
         * @brief 按目标控件当前的位置与可见性同步几何
         * @details 目标不可见时收起；可见时外扩 6px 并抬到同级最上层。
         */
        void syncGeometry();

    protected:
        /**
         * @brief 画焦点环
         * @param event 绘制事件，未使用
         * @details 重写 QWidget::paintEvent()：不铺底，只按当前形状描环；
         *          双环由贴边内环与 32% 透明度的外环组成，单环只画贴边那一圈。
         */
        void paintEvent(QPaintEvent *event) override;

    private:
        /**
         * @brief 目标控件的环中心矩形在宿主控件坐标系里的位置
         * @return 普通控件取自身矩形；只圈指示器时取复选框的 indicator 矩形
         * @details 目标与宿主之间可能隔着若干层行容器，不能直接用目标在直接父控件里的几何。
         */
        [[nodiscard]] QRect targetRectInHost() const;

        QPointer<QWidget> m_target;                                 ///< 被围绕的控件，销毁后自动置空
        FocusRingStyle    m_style         = FocusRingStyle::Double; ///< 当前环的形状
        bool              m_indicatorOnly = false;                  ///< 是否只圈复选框指示器
        ThemePalette      m_palette{};                              ///< 当前主题色
    };

    /**
     * @brief 应用级焦点环控制器：按焦点落点决定画哪种环
     * @details 作为应用事件过滤器使用。只有第一次聚焦到可映射控件时才创建覆盖层；
     *          失焦、目标隐藏或切到高对比度档时收起。控件销毁时覆盖层随宿主或目标一起回收。
     */
    class FocusRingController final : public QObject
    {
        Q_OBJECT

    public:
        /// 取当前主题色板的回调；主题切换时重新取一次
        using PaletteProvider = std::function<ThemePalette()>;

        /**
         * @brief 构造控制器
         * @param palette 取当前主题色板的回调，必须活过控制器
         * @param parent Qt 父对象
         */
        explicit FocusRingController(PaletteProvider palette, QObject *parent = nullptr);

        /**
         * @brief 销毁控制器并同步删掉仍活着的覆盖层
         * @details 收尾阶段不再跑事件循环，deleteLater 投递的删除不会执行；这里直接删除，
         *          避免覆盖层留到宿主控件析构。
         */
        ~FocusRingController() override;

        /**
         * @brief 声明当前主题档位
         * @param kind 解算后的主题档位
         * @details 高对比度档直接收起覆盖层，焦点完全交给 QSS 的 2px 描边。
         */
        void setThemeKind(ThemeKind kind);

        /**
         * @brief 应用事件过滤器：跟踪焦点、移动、缩放与显隐
         * @param watched 事件目标
         * @param event 事件
         * @return 恒为 false，不改写任何事件
         * @details 重写 QObject::eventFilter()：只旁听焦点与几何变化，不拦截事件。
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        /// 一次焦点落点映射出的覆盖层规格
        struct TargetSpec
        {
            QWidget       *widget        = nullptr;                ///< 环围绕的控件
            FocusRingStyle style         = FocusRingStyle::Double; ///< 双环或单环
            bool           indicatorOnly = false;                  ///< 是否只圈复选框指示器
        };

        /**
         * @brief 把焦点控件映射成覆盖层规格
         * @param focused 当前拿到焦点的控件
         * @return 需要画环时返回规格；不该画环时返回空
         * @details 可编辑下拉框与微调框的内部行编辑映射回外层控件；只读、禁用与
         *          标注工具栏内的控件一律不画。
         */
        [[nodiscard]] std::optional<TargetSpec> resolveTarget(QWidget *focused) const;

        /**
         * @brief 为规格创建并显示覆盖层
         * @param spec 目标控件与环的形状
         */
        void showFor(const TargetSpec &spec);

        /// 收起并回收当前覆盖层
        void hideRing();

        PaletteProvider            m_palette;                      ///< 主题色板来源
        ThemeKind                  m_themeKind = ThemeKind::Light; ///< 当前档位
        QPointer<QWidget>          m_target;                       ///< 当前围绕的控件
        QPointer<FocusRingOverlay> m_ring;                         ///< 当前覆盖层
    };
} // namespace GyanisScribe::Gui
