#include "App/ImageFetcher.h"

#include "Base/Text.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <format>

namespace GyanisScribe::App
{
    bool isFetchableImageUrl(const QUrl &url)
    {
        return url.isValid() && !url.host().isEmpty() && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
    }

    ImageFetcher::ImageFetcher(QObject *parent) : QObject(parent), m_network(std::make_unique<QNetworkAccessManager>())
    {
    }

    ImageFetcher::~ImageFetcher()
    {
        // 先摘掉在飞那一条的信号：销毁管理器会把回复一并收掉，那时回调还会进来读已经析构的成员
        if (m_reply != nullptr)
        {
            m_reply->disconnect();
        }
    }

    bool ImageFetcher::isBusy() const noexcept
    {
        return m_reply != nullptr;
    }

    void ImageFetcher::fetch(const QUrl &url)
    {
        // 上一条还在飞：先给它一个明确收场再换新的。截图工具不是下载器，同时跟两条只会让提示分不清哪一句属于谁
        if (m_reply != nullptr)
        {
            concludeWithFailure(GyanisScribe::Base::uiText("上一个图片请求尚未完成就被新地址替换，该图片未获取。如需此图片，请重新粘贴。"));
        }

        m_payload.clear();
        m_overSize = false;
        m_source   = url;

        QNetworkRequest request{url};
        // 超时与降级跳转都不跟：https 跳到 http 等于把「取的是哪个地址的图」悄悄换掉
        request.setTransferTimeout(kImageFetchTimeoutMilliseconds);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_reply = m_network->get(request);
        watch(m_reply);
    }

    void ImageFetcher::concludeWithFailure(const QString &reason)
    {
        if (m_reply == nullptr)
        {
            return;
        }
        auto *reply = m_reply;
        m_reply     = nullptr;
        reply->abort();
        reply->deleteLater();
        emit failed(reason);
    }

    void ImageFetcher::watch(QNetworkReply *reply)
    {
        QObject::connect(reply, &QNetworkReply::readyRead, this,
                         [this, reply]
                         {
                             if (m_reply != reply)
                             {
                                 return;
                             }
                             m_payload += reply->readAll();
                             // 大小闸门按「已经收下来的字节」算：只看 Content-Length 挡不住分块传输那种不报长度的响应
                             if (m_payload.size() > kMaximumFetchedImageBytes)
                             {
                                 m_overSize = true;
                                 reply->abort(); // 收场统一走 finished
                             }
                         });

        QObject::connect(reply, &QNetworkReply::finished, this,
                         [this, reply]
                         {
                             if (m_reply != reply)
                             {
                                 reply->deleteLater();
                                 return;
                             }

                             const auto source  = m_source;
                             m_reply            = nullptr;
                             const auto cleanup = [reply] { reply->deleteLater(); };

                             if (m_overSize)
                             {
                                 cleanup();
                                 emit failed(QString::fromUtf8(
                                         GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("图片超过 {} MB 上限，未获取。请更换较小的图片，或先保存到本地再拖入。"),
                                                                      kMaximumFetchedImageBytes / (1024LL * 1024))
                                                 .c_str()));
                                 return;
                             }

                             if (reply->error() != QNetworkReply::NoError)
                             {
                                 cleanup();
                                 // 超时那条 Qt 只给一个「操作被取消」，这里按是否设过超时分开说，用户才知道是网络慢还是地址坏
                                 if (reply->error() == QNetworkReply::OperationCanceledError)
                                 {
                                     emit failed(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8(
                                                                                                        "取这张图超过 {} 秒还没回来，已经放弃：检查网络，或先把它存到盘上再拖进来"),
                                                                                                kImageFetchTimeoutMilliseconds / 1000)
                                                                           .c_str()));
                                     return;
                                 }
                                 emit failed(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("未能获取图片：{}。请检查该地址能否在浏览器中打开。"),
                                                                                            reply->errorString().toStdString())
                                                                       .c_str()));
                                 return;
                             }

                             // 认 content-type：地址后缀不作数（一多半图片地址看不出后缀），读不出类型的按二进制放行、交给下面的解码判
                             const auto contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().section(QLatin1Char(';'), 0, 0).trimmed().toLower();
                             if (!contentType.isEmpty() && !contentType.startsWith(QLatin1String("image/")) && contentType != QLatin1String("application/octet-stream"))
                             {
                                 cleanup();
                                 emit failed(QString::fromUtf8(
                                         GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("该地址返回的内容不是图片（{}）。如需粘贴网页图片，请右键单击图片并复制图片地址。"),
                                                                      contentType.toStdString())
                                                 .c_str()));
                                 return;
                             }

                             QImage image;
                             if (!image.loadFromData(m_payload))
                             {
                                 cleanup();
                                 emit failed(GyanisScribe::Base::uiText("无法读取返回数据的像素（可能不是图片，或当前环境不支持该格式）。请更换图片地址后重试。"));
                                 return;
                             }

                             cleanup();
                             emit fetched(ImageFetchResult{image, source});
                         });
    }

} // namespace GyanisScribe::App
