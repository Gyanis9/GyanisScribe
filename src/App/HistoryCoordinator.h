/**
 * @file HistoryCoordinator.h
 * @brief 历史清单这一族从控制器里搬出来的落脚处（审查台账 A-11）
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/History/HistoryTypes.h"
#include "Gui/Controls/Toast.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <filesystem>

namespace GyanisScribe::Core
{
    class HistoryStore;
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::App
{

    /**
     * @brief 历史清单的增删查与「这条去哪了」
     * @details 只做清单本身的事：记一条、去掉一条、清空、打开所在目录；要贴到屏上的那一下
     *          走 pinRequested 交回控制器（贴图那一族的地盘），要出给用户的话走 messageForUser。
     *          清单变了发 changed()——**不去直接叫窗口刷新**：那会让 App 层反向依赖 Main，
     *          而 Main 本来就依赖 App。
     */
    class HistoryCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param history 清单本体；**所有权不在这里**——同一个 store 也交给主窗口读，两边认同一份
         * @param settings 保留策略（留多少条、多少天）从这里读
         * @param parent Qt 父对象
         */
        HistoryCoordinator(GyanisScribe::Core::HistoryStore &history, GyanisScribe::Core::ISettingsStore &settings, QObject *parent = nullptr);

        /**
         * @brief 记一条成品
         * @param filePath 成品路径
         * @param kind 归到哪一类（截图/录屏）
         * @param size 像素尺寸
         * @details 清单写不进去不该让「保存成功」变成假话：成品已经在盘上，只是下次打开找不到它——
         *          所以这里报一声而不是静默。
         */
        void record(const std::filesystem::path &filePath, GyanisScribe::Core::HistoryKind kind, const QSize &size);

        /// 去掉一条记录（磁盘上的成品保留）
        void remove(const std::filesystem::path &filePath);

        /// 清空清单（磁盘上的成品保留，与单条删除同一条口径）
        void clear();

        /// 打开这条成品所在的目录
        void openContainingFolder(const std::filesystem::path &filePath);

        /**
         * @brief 把这条成品贴到屏上
         * @param filePath 成品路径
         * @details 贴到哪儿、贴成什么由控制器决定（它才知道光标在哪、贴图那一族怎么用）；
         *          读不出来在这里就报，别把一张空图交出去。
         */
        void requestPin(const std::filesystem::path &filePath);

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);
        /// 清单变了：控制器据此让窗口重读
        void changed();
        /// 有人要求把这张图贴到屏上：带上盘上的来源文件，动图贴上去要接着播
        void pinRequested(const QImage &image, const QString &sourceFile);

    private:
        GyanisScribe::Core::HistoryStore   &m_history;  ///< 清单本体，不拥有
        GyanisScribe::Core::ISettingsStore &m_settings; ///< 保留策略的来源，不拥有
    };

} // namespace GyanisScribe::App
