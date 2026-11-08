#pragma once

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/ControlPalette.h"

#include <QColor>
#include <QLabel>
#include <QWidget>

namespace GyanisScribe::Gui
{
    /**
     * @brief 按连续曲率铺一层语义色底的通用容器
     * @details QSS 只能画方角或普通圆角，设置页卡片与浮层要的是同一套连续曲率路径；
     *          本类只负责底与描边，内容与版面仍由调用方塞进布局。
     */
    class SurfaceWidget : public QWidget
    {
    public:
        /**
         * @brief 构造一块面
         * @param role 圆角档位
         * @param fillRole 铺哪一档语义色
         * @param parent Qt 父对象
         */
        explicit SurfaceWidget(Core::CornerRole role, SurfaceFillRole fillRole, QWidget *parent = nullptr);

        /**
         * @brief 换铺底角色
         * @param role 新的铺底角色
         */
        void setFillRole(SurfaceFillRole role);

        /**
         * @brief 设可见描边色
         * @param color 描边色；无效色表示不画边
         */
        void setBorderColor(const QColor &color);

        /**
         * @brief 设可见描边宽度
         * @param width 描边宽度，逻辑像素；0 表示不画边
         */
        void setBorderWidth(int width);

        /**
         * @brief 让高对比度档自动补 borderStrong 2px 边
         * @param enabled true 表示这一档由本控件补边
         */
        void setHighContrastBorder(bool enabled);

    protected:
        /**
         * @brief 按当前角色与铺底色画连续曲率底
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;

    private:
        Core::CornerRole m_role;                       ///< 圆角档位
        SurfaceFillRole  m_fillRole;                   ///< 铺底角色
        QColor           m_borderColor;                ///< 可见描边色
        int              m_borderWidth        = 0;     ///< 可见描边宽度
        bool             m_highContrastBorder = false; ///< 高对比度档是否自动补边
    };

    /**
     * @brief 带连续曲率底色的标签
     * @details 品牌底板这类「一块色底上压一张图或一段字」的控件用它；
     *          底画在标签自己的 paintEvent 里，字体与对齐仍走 QLabel。
     */
    class SurfaceLabel : public QLabel
    {
    public:
        /**
         * @brief 构造一块标签底面
         * @param fillRole 铺哪一档语义色
         * @param role 圆角档位
         * @param parent Qt 父对象
         */
        explicit SurfaceLabel(SurfaceFillRole fillRole, Core::CornerRole role, QWidget *parent = nullptr);

        /**
         * @brief 换铺底角色
         * @param role 新的铺底角色
         */
        void setFillRole(SurfaceFillRole role);

        /**
         * @brief 设可见描边色
         * @param color 描边色；无效色表示不画边
         */
        void setBorderColor(const QColor &color);

        /**
         * @brief 让高对比度档自动补 borderStrong 2px 边
         * @param enabled true 表示这一档由本控件补边
         */
        void setHighContrastBorder(bool enabled);

    protected:
        /**
         * @brief 先画连续曲率底，再让 QLabel 画内容
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;

    private:
        SurfaceFillRole  m_fillRole;                   ///< 铺底角色
        Core::CornerRole m_role;                       ///< 圆角档位
        QColor           m_borderColor;                ///< 可见描边色
        bool             m_highContrastBorder = false; ///< 高对比度档是否自动补边
    };
} // namespace GyanisScribe::Gui
