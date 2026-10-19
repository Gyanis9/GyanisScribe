/**
 * @file DeliveryCoordinator.h
 * @brief 交付这一族从控制器里搬出来的落脚处
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Gui/Controls/Toast.h"

#include <QImage>
#include <QObject>
#include <QString>

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    struct PinBatchSaveReport;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{
    class HistoryCoordinator;
} // namespace GyanisScribe::App

namespace GyanisScribe::App
{

    /**
     * @brief 成品往盘上落这一族：装饰、起名、编码，以及落盘之后那两件事（外部命令、批量报告）
     * @details 三条落盘路（标注窗的保存、截图后的快速保存、命令行交付）共用同一处出口，
     *          用户看到的措辞才不会三处各说各的。要出给用户的话走 messageForUser。
     *          「最近落盘的那份路径」由这一族自己记着：外部命令的 `%file%` 与命令行打到标准输出的
     *          都是它，两处各算一次就会在装饰开关上分家。
     */
    class DeliveryCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param settings 输出目录、格式、质量与装饰档从设置现读
         * @param history 成品落盘后记进历史（记的是盘上那份的尺寸，见 saveToDisk）
         * @param parent Qt 父对象
         */
        DeliveryCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::App::HistoryCoordinator &history, QObject *parent = nullptr);

        /**
         * @brief 存一张图到设置里那个目录
         * @param image 要存的图（未装饰）
         * @return 起好的文件名；失败时返回带中文原因的错误（三档口径合成一句「没能存下图片：…」）
         * @details 装饰按目标端要不要透明通道来定：JPG 垫白，PNG/WebP 保持透明（写出去不丢那一层）。
         *          历史记的是盘上那份的尺寸：开着装饰时成品比原图大一围，记小了缩略图会裁边。
         */
        [[nodiscard]] GyanisScribe::Base::Expected<QString> saveToDisk(const QImage &image);

        /// 保存这一条入口（标注窗的「保存文件」与截图后的「直接存成文件」共用）：成了说存到哪儿，失败说原因
        void saveAndReport(const QImage &image);

        /// 按目标端要不要透明通道装饰一层（边框与投影）：剪贴板与保存两路各按自己的口径取一次
        [[nodiscard]] QImage decorate(const QImage &image, bool fillBackground) const;

        /**
         * @brief 截图后跑设置里那条外部命令
         * @param commandLineOverride 非空即以它为准（命令行给了 `--exec` 就只跑那一条：
         *        与设置里那一格叠着跑会跑出两份副作用，而脚本要的是「这一次照我说的做」）
         */
        void runAfterCaptureCommand(const QString &commandLineOverride);

        /// 贴图批量保存的收尾一句话：全写成、成一半、一张都没成各一句（一张都没成时原因就是主角）
        void reportBatchSave(const GyanisScribe::Gui::PinBatchSaveReport &report);

        /// 最近一次落盘的成品路径（显示文本）；没落盘时为空
        [[nodiscard]] const QString &writtenFilePath() const noexcept;

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    private:
        GyanisScribe::Core::ISettingsStore    &m_settings;        ///< 输出档位来源，不拥有
        GyanisScribe::App::HistoryCoordinator &m_history;         ///< 落盘后记账，不拥有
        QString                                m_writtenFilePath; ///< 最近落盘那份的显示路径
    };

} // namespace GyanisScribe::App
