/**
 * @file PickCoordinator.h
 * @brief 取色这一族从控制器里搬出来的落脚处（审查台账 A-11）
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Gui/Pick/ColorPickerOverlay.h"
#include "Gui/Controls/Toast.h"

#include <QColor>
#include <QObject>
#include <QPointer>

#include <vector>

namespace GyanisScribe::Core
{
    class IScreenCaptureSource;
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class ColorPickerOverlay;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{

    /**
     * @brief 屏幕取色一条链：抓一帧当下 → 铺全屏遮罩 → 点哪抄哪 → 进「最近取色」
     * @details 遮罩那一档的写法、字母档与放大镜视野全都现读设置：读坏了不拦取色，按默认档继续
     *          并把原因说给用户——他复制到的格式莫名其妙变了才是更难查的事。要出给用户的话走 messageForUser。
     */
    class PickCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param settings 写法、字母档、放大镜视野与「最近取色」都从这里读
         * @param theme 遮罩的主题来源
         * @param captureSource 抓帧来源：遮罩显示的必须是按下那一刻的那一屏
         * @param parent Qt 父对象
         */
        PickCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::ThemeManager &theme, GyanisScribe::Core::IScreenCaptureSource &captureSource,
                        QObject *parent = nullptr);

        /**
         * @brief 起一层全屏取色遮罩（热键、托盘与命令行共用这一条）
         * @details 抓的是按下那一刻的那一屏，之后遮罩显示的就是这一帧：放大镜要看的正是当前画面，
         *          实时抓屏会让鼠标一停画面就变，颜色也就抄不准。
         */
        void start();

        /**
         * @brief 贴图上取到的色也走这一条：进同一份「最近取色」，再按设置里的写法抄进剪贴板
         * @param color 贴图上取到的颜色
         */
        void copyPickedColor(const QColor &color);

        /// 最近取色清单（设置里那一格）；读不出时按空清单继续并落一条警告
        [[nodiscard]] std::vector<QColor> recentColors() const;

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    private:
        /// 压进「最近取色」清单并写回设置：贴图上取色与屏幕取色共用这一处
        void remember(const QColor &color);

        GyanisScribe::Core::ISettingsStore             &m_settings;      ///< 写法与清单来源，不拥有
        GyanisScribe::Gui::ThemeManager                &m_theme;         ///< 遮罩主题，不拥有
        GyanisScribe::Core::IScreenCaptureSource       &m_captureSource; ///< 抓帧来源，不拥有
        QPointer<GyanisScribe::Gui::ColorPickerOverlay> m_picker;        ///< 取色遮罩，关掉即重建
    };

} // namespace GyanisScribe::App
