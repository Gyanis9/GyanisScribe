/**
 * @file ThemeManager.h
 * @brief 主题管理器：档位落配置、样式表装载与切换后的通知
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Ui/DesignTokens.h"
#include "Ui/ThemeKind.h"

#include <QObject>

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{

    /**
     * @brief 把主题档位落到界面：写配置、换样式表、通知各界面重取色
     * @details 一个进程只有一个是活跃的：App 持有它，IconProvider 与自绘控件都从它拿色。
     *          切换不需要重启——样式表整片重设，自绘部分靠 themeChanged 重取。
     */
    class ThemeManager : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造管理器并接上配置变更
         * @param settings 配置存储，生命周期由 App 保证，必须活过本对象
         * @param parent Qt 父对象
         * @note 构造不做任何应用层动作：档位由调用方随后 applyFromSettings() 取，避免半路覆盖界面已定的样式
         */
        explicit ThemeManager(Settings::ISettingsStore &settings, QObject *parent = nullptr);

        /**
         * @brief 从配置读档位并立刻生效
         * @details 存的文本不是三个可用取值之一时记一条 Warning 再按 System 档走：
         *          坏值来自手改配置，界面不能因此停在无主题状态。
         */
        void applyFromSettings();

        /**
         * @brief 用户改档位：写回配置并立即生效
         * @param themeKind 选定的档位
         * @return 写配置失败时返回原因；成功返回空载荷
         */
        Base::Expected<void> setTheme(ThemeKind themeKind);

        /**
         * @brief 当前生效的语义色
         * @return 已解算档位的调色板引用，随主题切换更换
         */
        [[nodiscard]] const ThemePalette &palette() const;

        /**
         * @brief 图标描边色
         * @return 当前调色板里的 icon 色，交给 IconProvider::setTintColor
         */
        [[nodiscard]] QColor iconTint() const;

        /// 用户选的档位（System 或明确指定的某一档）
        [[nodiscard]] ThemeKind requestedKind() const;

        /// 解算后真正生效的档位，只会是 Light 或 Dark
        [[nodiscard]] ThemeKind resolvedKind() const;

        /**
         * @brief 读出一套主题的界面样式表
         * @param resolvedKind 已解算的档位，System 会按浅色取
         * @return QSS 文本；资源缺失时返回空串并记一条含资源路径的错误日志
         */
        [[nodiscard]] static QString styleSheetFor(ThemeKind resolvedKind);

    signals:
        /**
         * @brief 主题已切换，自绘控件与图标供给器该重取色
         */
        void themeChanged();

    private:
        /**
         * @brief 按当前档位解算、落样式表并发出通知
         * @details 解算结果与上一次相同时整段跳过：样式表整片重设会让所有控件重排重绘，
         *          没有色变化就不该付这个代价。
         */
        void refresh();

        /// 读系统回报的深浅色偏好
        [[nodiscard]] SystemScheme currentSystemScheme() const;

        Settings::ISettingsStore &m_settings;  ///< 配置存储，档位从这里读、也写回这里
        ThemeKind                 m_requested; ///< 用户选的档位
        ThemeKind                 m_resolved;  ///< 解算后生效的档位
    };

} // namespace GyanisScribe::Ui
