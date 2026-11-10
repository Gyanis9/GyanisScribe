#include "App/UpdateCheck.h"

#include "Base/Logger.h"
#include "Base/Text.h"

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
        /// 把网络错误枚举换成可读名：文案里塞一个数字用户没法照做
        [[nodiscard]] QString errorStringOf(const QNetworkReply::NetworkError error)
        {
            switch (error)
            {
                case QNetworkReply::TimeoutError:
                    return QString::fromUtf8(Base::uiTextUtf8("超时"));
                case QNetworkReply::HostNotFoundError:
                    return QString::fromUtf8(Base::uiTextUtf8("域名解析失败"));
                case QNetworkReply::ConnectionRefusedError:
                    return QString::fromUtf8(Base::uiTextUtf8("连接被拒绝"));
                case QNetworkReply::TemporaryNetworkFailureError:
                case QNetworkReply::NetworkSessionFailedError:
                case QNetworkReply::RemoteHostClosedError:
                    return QString::fromUtf8(Base::uiTextUtf8("连接中断"));
                default:
                    return QString::fromUtf8(Base::uiTextUtf8("网络错误"));
            }
        }

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

    Base::Expected<UpdateRelease> parseLatestRelease(const QByteArray &payload)
    {
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject())
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("检查更新的响应不是有效的发布信息（不是 JSON 对象）：可能是网络请求被拦截，请稍后重试")});
        }

        const auto object = document.object();
        const auto tag    = object.value(QStringLiteral("tag_name"));
        if (!tag.isString() || tag.toString().trimmed().isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("发布信息中缺少版本标签：请前往发布页查看是否有新版本")});
        }

        const auto page = object.value(QStringLiteral("html_url"));
        if (!page.isString() || page.toString().trimmed().isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("发布信息中缺少发布页地址：请前往发布页查看是否有新版本")});
        }

        return UpdateRelease{.tagName = tag.toString().trimmed(), .pageUrl = page.toString().trimmed()};
    }

    QString failureMessageFor(const QNetworkReply::NetworkError error, const int httpStatus)
    {
        switch (error)
        {
            case QNetworkReply::ContentNotFoundError:
                // 私有仓库与「还没有发布过正式版」在未鉴权的接口上是同一个 404，两种都只能这么提示
                return QString::fromUtf8(Base::uiTextUtf8("无法获取发布信息（404）：仓库尚未公开，或尚未发布正式版。请前往发布页查看"));
            case QNetworkReply::AuthenticationRequiredError:
            case QNetworkReply::ContentAccessDenied:
                return QString::fromUtf8(
                        Base::uiFormat(Base::uiTextUtf8("GitHub 拒绝了本次请求（HTTP {}）：可能是请求过于频繁，或网络请求被拦截。请稍后重试，或直接访问发布页"), httpStatus)
                                .c_str());
            case QNetworkReply::TimeoutError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::RemoteHostClosedError:
                return QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("无法连接 GitHub（{}）：请检查网络或代理后重试，或直接访问发布页"), errorStringOf(error).toStdString()).c_str());
            default:
                return QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("检查更新失败（{}）：请稍后重试，或直接访问发布页"), errorStringOf(error).toStdString()).c_str());
        }
    }

    UpdateVerdict failureVerdictFor(const QNetworkReply::NetworkError error, const int httpStatus)
    {
        return UpdateVerdict{false, failureMessageFor(error, httpStatus), QString::fromLatin1(kReleasePageUrl.data(), static_cast<qsizetype>(kReleasePageUrl.size()))};
    }

    UpdateVerdict verdictFor(const QString &runningVersion, const UpdateRelease &release)
    {
        const auto running = QVersionNumber::fromString(versionTextOf(runningVersion));
        if (running.isNull())
        {
            return UpdateVerdict{
                    false,
                    QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("无法读取本机版本号「{}」，无法与最新版本比较。请前往发布页查看"), runningVersion.toStdString()).c_str()),
                    QString{}};
        }

        const auto latestText = versionTextOf(release.tagName);
        const auto latest     = QVersionNumber::fromString(latestText);
        if (latest.isNull())
        {
            return UpdateVerdict{false,
                                 QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("无法读取发布页上的版本标签「{}」，无法与本机版本比较"), release.tagName.toStdString()).c_str()),
                                 QString{}};
        }

        if (QVersionNumber::compare(latest, running) > 0)
        {
            return UpdateVerdict{
                    true,
                    QString::fromUtf8(
                            Base::uiFormat(Base::uiTextUtf8("有新版本 {}（本机 {}）：点击该行打开下载页"), latestText.toStdString(), runningVersion.toStdString()).c_str()),
                    release.pageUrl};
        }
        return UpdateVerdict{false, QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("已是最新（{}）"), runningVersion.toStdString()).c_str()), QString{}};
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
                                 // 英文原文只进日志：界面上那行要给中文原因与下一步做法
                                 const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                                 Base::Logger::warning(std::format("检查更新失败：{}（HTTP {}）", reply->errorString().toStdString(), status));
                                 emit finished(failureVerdictFor(reply->error(), status));
                                 reply->deleteLater();
                                 return;
                             }

                             const auto parsed = parseLatestRelease(reply->readAll());
                             if (!parsed)
                             {
                                 Base::Logger::warning(std::format("检查更新的回应读不出来：{}", parsed.error().message));
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
