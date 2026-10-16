#include "App/DeliveryCoordinator.h"

#include "App/HistoryCoordinator.h"
#include "Base/ExternalCommand.h"
#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Image/ImageFormat.h"
#include "Core/Image/OutputDecoration.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/OutputStyleRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Pin/PinManager.h"

#include <QDateTime>

#include <format>

namespace GyanisScribe::App
{
    DeliveryCoordinator::DeliveryCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::App::HistoryCoordinator &history, QObject *parent) :
        QObject(parent), m_settings(settings), m_history(history)
    {
    }

    GyanisScribe::Base::Expected<QString> DeliveryCoordinator::saveToDisk(const QImage &image)
    {
        // 失败一律带上「没能存下图片」这句头：三条调用路共用一处出口，用户看到的措辞才不会三处各说各的
        const auto fail = [](const GyanisScribe::Base::Error &reason)
        {
            const auto message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("没能存下图片：{}"), reason.message);
            GyanisScribe::Base::Logger::error(message);
            return std::unexpected(GyanisScribe::Base::Error{reason.code, message});
        };

        const auto                  configured = m_settings.stringValue(GyanisScribe::Core::kPathScreenshot, QString{});
        const std::filesystem::path directory =
                configured.isEmpty() ? GyanisScribe::Base::Paths::defaultScreenshotDirectory() : GyanisScribe::Base::Paths::fromDisplayText(configured);
        if (const auto ensured = GyanisScribe::Base::Paths::ensureDirectory(directory); !ensured)
        {
            return fail(ensured.error());
        }

        const auto storedFormat = m_settings.stringValue(GyanisScribe::Core::kSaveImageFormat, QStringLiteral("Png"));
        const auto format       = GyanisScribe::Core::parseImageFormat(storedFormat);
        if (!format)
        {
            // 不猜一档：猜错就是把成品按另一种压缩写出去，事后无从找回原样
            return fail(format.error());
        }
        const auto quality = m_settings.intValue(GyanisScribe::Core::kSaveImageQuality, GyanisScribe::Core::kDefaultImageQuality);
        if (const auto checked = GyanisScribe::Core::parseImageQuality(quality); !checked)
        {
            return fail(checked.error());
        }
        if (!GyanisScribe::Core::isImageFormatWritable(*format))
        {
            // 这一档的编码器没装：说清是格式的问题，别让用户去查目录权限
            return fail(GyanisScribe::Base::Error{
                    GyanisScribe::Base::ErrorCode::PlatformUnsupported,
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("这台机器的 Qt 写不出「{}」这一档。请在设置的截图页改选一档"), storedFormat.toStdString())});
        }

        // 装饰按目标端要不要透明通道来定：JPG 垫白，PNG/WebP 保持透明（写出去不丢那一层）
        const auto decorated = decorate(image, !GyanisScribe::Core::imageFormatHasAlpha(*format));

        // 名字规则来自设置里那一格（留空即默认档）；起好名再让一次位：模板里去掉 %ms% 之后
        // 同一秒连拍会撞名，直接写就是把前一张悄悄盖掉
        const auto fileName = GyanisScribe::Base::Paths::imageFileName(QDateTime::currentDateTime(), GyanisScribe::Core::imageFormatExtension(*format),
                                                                       GyanisScribe::Core::nameTemplateOrDefault(m_settings));
        const auto filePath = GyanisScribe::Base::Paths::uniqueFilePath(directory, fileName);
        // 格式不单独传给 Qt：它按后缀挑编码器，再传一次格式名就是第二个出处
        if (!decorated.save(GyanisScribe::Base::Paths::toDisplayText(filePath), nullptr, quality))
        {
            return fail(GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::FileWriteFailed,
                                                  GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("「{}」写不进去。请检查这个目录能不能写"),
                                                                               GyanisScribe::Base::Paths::toDisplayText(filePath).toStdString())});
        }
        GyanisScribe::Base::Logger::info(std::format("成品已保存：{}", GyanisScribe::Base::Paths::toDisplayText(filePath).toStdString()));
        // 历史记的是盘上那份的尺寸：开着装饰时成品比原图大一围，记小了缩略图会裁边
        m_history.record(filePath, GyanisScribe::Core::HistoryKind::Screenshot, decorated.size());
        // 路径留在成员上，三条落盘路（存文件/快速保存/自动保存）与命令行的交付共用这一处读数：
        // 外部命令的 %file% 与命令行打到标准输出的都是它，两处各算一次就会在装饰开关上分家
        m_writtenFilePath = GyanisScribe::Base::Paths::toDisplayText(filePath);
        return fileName;
    }

    void DeliveryCoordinator::saveAndReport(const QImage &image)
    {
        const auto saved = saveToDisk(image);
        if (!saved)
        {
            emit messageForUser(QString::fromStdString(saved.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已存成 {}"), saved->toStdString()).c_str()),
                            GyanisScribe::Gui::ToastKind::Success);
    }

    QImage DeliveryCoordinator::decorate(const QImage &image, const bool fillBackground) const
    {
        const auto border = m_settings.boolValue(GyanisScribe::Core::kOutputBorder, false);
        const auto shadow = m_settings.boolValue(GyanisScribe::Core::kOutputShadow, false);
        // 宽与色是一份参数、一处读出来：画布尺寸与绘制拿到同一份，才不会出现「框按新值、尺寸按旧值」的错位
        return GyanisScribe::Core::decorateForOutput(image, border, shadow, fillBackground, GyanisScribe::Core::readOutputDecorationStyle(m_settings));
    }

    void DeliveryCoordinator::runAfterCaptureCommand(const QString &commandLineOverride)
    {
        // 命令行给了 --exec 就只跑那一条：与设置里那一格叠着跑会跑出两份副作用，而脚本要的是「这一次照我说的做」
        const auto commandLine = commandLineOverride.isEmpty() ? m_settings.stringValue(GyanisScribe::Core::kCaptureExecCommand, QString{}) : commandLineOverride;
        if (commandLine.trimmed().isEmpty())
        {
            return;
        }

        const auto planned = GyanisScribe::Base::planExternalCommand(commandLine, m_writtenFilePath);
        if (!planned)
        {
            GyanisScribe::Base::Logger::error(std::format("截图后的外部命令没拆开：{}", planned.error().message));
            emit messageForUser(QString::fromStdString(planned.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        const auto started = GyanisScribe::Base::startExternalCommand(*planned);
        if (!started)
        {
            GyanisScribe::Base::Logger::error(std::format("截图后的外部命令没起跑：{}", started.error().message));
            emit messageForUser(QString::fromStdString(started.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        GyanisScribe::Base::Logger::info(std::format("截图后已交外部程序起跑：{}", planned->program.toStdString()));
    }

    void DeliveryCoordinator::reportBatchSave(const GyanisScribe::Gui::PinBatchSaveReport &report)
    {
        // 三档收尾各一句：全写成要说「去哪儿看」，成了一半要点名还有几张没成，一张都没成时原因就是主角
        if (report.saved == 0)
        {
            const auto reason = report.reason.isEmpty() ? QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("原因见日志")) : report.reason;
            emit       messageForUser(QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("贴图一张都没写成：")) + reason, GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        if (report.failed == 0)
        {
            emit messageForUser(
                    QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已把 {} 张贴图存成文件，就在设置里的截图目录"), report.saved).c_str()),
                    GyanisScribe::Gui::ToastKind::Success);
            return;
        }
        const auto reason = report.reason.isEmpty() ? QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("原因见日志")) : report.reason;
        emit       messageForUser(
                QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已存好 {} 张贴图，另有 {} 张没写成："), report.saved, report.failed).c_str()) +
                        reason,
                GyanisScribe::Gui::ToastKind::Danger);
    }

    const QString &DeliveryCoordinator::writtenFilePath() const noexcept
    {
        return m_writtenFilePath;
    }

} // namespace GyanisScribe::App
