#include "App/UpdateCheck.h"

#include "Core/Logger.h"
#include "Core/Text.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVersionNumber>

#include <format>

namespace GyanisScribe::App
{
    namespace
    {
        /// 版本号前那个 v 是标签的写法，比较前要去掉（大小写都认，GitHub 上两种都见过）
        [[nodiscard]] QString versionTextOf(const QString &tag)
        {
            auto text = tag.trimmed();
            if (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V')))
            {
                text.remove(0, 1);
            }
            return text;
        }
    } // namespace

    Core::Expected<UpdateRelease> parseLatestRelease(const QByteArray &payload)
    {
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, Core::uiTextUtf8("更新检查的回应不是一份发布信息（不是 JSON 对象）：多半是网络中间被拦了，请稍后再试")});
        }

        const auto object = document.object();
        const auto tag    = object.value(QStringLiteral("tag_name"));
        if (!tag.isString() || tag.toString().trimmed().isEmpty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, Core::uiTextUtf8("发布信息里没有版本标签：请到发布页看有没有新版")});
        }

        const auto page = object.value(QStringLiteral("html_url"));
        if (!page.isString() || page.toString().trimmed().isEmpty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, Core::uiTextUtf8("发布信息里没有发布页地址：请到发布页看有没有新版")});
        }

        return UpdateRelease{.tagName = tag.toString().trimmed(), .pageUrl = page.toString().trimmed()};
    }

    UpdateVerdict verdictFor(const QString &runningVersion, const UpdateRelease &release)
    {
        const auto running = QVersionNumber::fromString(versionTextOf(runningVersion));
        if (running.isNull())
        {
            return UpdateVerdict{
                    false,
                    QString::fromUtf8(Core::uiFormat(Core::uiTextUtf8("本机版本号「{}」读不出来，没法与最新版比。请到发布页自己看一眼"), runningVersion.toStdString()).c_str()),
                    QString{}};
        }

        const auto latestText = versionTextOf(release.tagName);
        const auto latest     = QVersionNumber::fromString(latestText);
        if (latest.isNull())
        {
            return UpdateVerdict{false,
                                 QString::fromUtf8(Core::uiFormat(Core::uiTextUtf8("发布页上的版本标签「{}」读不出来，没法与本机版本比"), release.tagName.toStdString()).c_str()),
                                 QString{}};
        }

        if (QVersionNumber::compare(latest, running) > 0)
        {
            return UpdateVerdict{
                    true,
                    QString::fromUtf8(
                            Core::uiFormat(Core::uiTextUtf8("有新版本 {}（本机 {}）：点这一行打开下载页"), latestText.toStdString(), runningVersion.toStdString()).c_str()),
                    release.pageUrl};
        }
        return UpdateVerdict{false, QString::fromUtf8(Core::uiFormat(Core::uiTextUtf8("已是最新（{}）"), runningVersion.toStdString()).c_str()), QString{}};
    }

    UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this))
    {
    }

    UpdateChecker::~UpdateChecker()
    {
        if (m_reply != nullptr)
        {
            // 析构时把还在飞的那条掐掉：让它把结果投回一个半死的对象，比这次检查没做完糟得多
            m_reply->abort();
        }
    }

    bool UpdateChecker::isRunning() const noexcept
    {
        return m_reply != nullptr;
    }

    void UpdateChecker::check()
    {
        if (m_reply != nullptr)
        {
            // 上一次还在飞：直接忽略这一按，不排队也不并发
            return;
        }

        QNetworkRequest request{QUrl{QString::fromLatin1(kReleaseApiUrl.data(), static_cast<qsizetype>(kReleaseApiUrl.size()))}};
        // GitHub 对没有 UA 的请求直接回 403；Accept 指明要 v3 的 JSON
        request.setRawHeader("User-Agent", QByteArrayLiteral("GyanisScribe-Updater/") + QCoreApplication::applicationVersion().toUtf8());
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setTransferTimeout(15000);

        m_reply              = m_network->get(request);
        QNetworkReply *reply = m_reply.data();
        QObject::connect(reply, &QNetworkReply::finished, this,
                         [this, reply]
                         {
                             if (reply->error() != QNetworkReply::NoError)
                             {
                                 const auto reason = reply->errorString();
                                 Core::Logger::warning(std::format("检查更新失败：{}", reason.toStdString()));
                                 emit finished(UpdateVerdict{
                                         false,
                                         QString::fromUtf8(Core::uiFormat(Core::uiTextUtf8("检查更新失败：{}。请稍后再试，或到发布页自己看一眼"), reason.toStdString()).c_str()),
                                         QString::fromLatin1(kReleasePageUrl.data(), static_cast<qsizetype>(kReleasePageUrl.size()))});
                                 reply->deleteLater();
                                 return;
                             }

                             const auto parsed = parseLatestRelease(reply->readAll());
                             if (!parsed)
                             {
                                 Core::Logger::warning(std::format("检查更新的回应读不出来：{}", parsed.error().message));
                                 emit finished(UpdateVerdict{false, QString::fromStdString(parsed.error().message),
                                                             QString::fromLatin1(kReleasePageUrl.data(), static_cast<qsizetype>(kReleasePageUrl.size()))});
                                 reply->deleteLater();
                                 return;
                             }

                             emit finished(verdictFor(QCoreApplication::applicationVersion(), *parsed));
                             reply->deleteLater();
                         });
    }

} // namespace GyanisScribe::App
