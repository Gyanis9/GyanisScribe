/**
 * @file ImageFetcher.h
 * @brief 从网页取一张图：只认 http/https、限大小、限时、认 content-type
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace GyanisScribe::App
{

    /// 取回来的图大小上限，字节：截图类图片通常几百 KB；超过这个数一律拒，也别让人拿一条地址把程序当下载器
    inline constexpr qint64 kMaximumFetchedImageBytes = 32LL * 1024 * 1024;

    /// 一次取图的超时，毫秒：网络慢就报错收场，不把这一条挂在漫长的等待上
    inline constexpr int kImageFetchTimeoutMilliseconds = 15000;

    /**
     * @brief 这条地址值不值得去取
     * @param url 待判的地址
     * @return 只有 http/https、且带主机名时为真
     * @details 本地路径有它自己那条路（读文件），`file:` 与其它协议一律不在这条里冒充：
     *          放开 `file:` 就等于把「贴一条地址」变成「把这台机器上的任意文件读进来贴上屏幕」。
     */
    [[nodiscard]] bool isFetchableImageUrl(const QUrl &url);

    /**
     * @brief 取回来的一张图
     */
    struct ImageFetchResult
    {
        QImage image;  ///< 取到的像素
        QUrl   source; ///< 它的来源地址（提示里要说清取自哪儿）
    };

    /**
     * @brief 异步取图器
     * @details 同一时刻只跟一条：新的取图请求把上一条收掉（被收掉的那条也会收到一次 failed）。
     *          失败一律给中文可操作文案：地址不对、超时、太大、不是图片、像素读不出来各说各的。
     */
    class ImageFetcher : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造函数
         * @param parent Qt 父对象
         */
        explicit ImageFetcher(QObject *parent = nullptr);

        /**
         * @brief 析构：先收掉在飞的那一条，再随本类销毁管理器
         */
        ~ImageFetcher() override;

        /**
         * @brief 去把这张图取回来
         * @param url 图片地址，调用方已用 isFetchableImageUrl 粗筛过
         */
        void fetch(const QUrl &url);

        /**
         * @brief 这一条取图是不是还在飞
         * @return 有在飞的一条时为真
         */
        [[nodiscard]] bool isBusy() const noexcept;

    signals:
        /// 取到了
        void fetched(const GyanisScribe::App::ImageFetchResult &result);

        /// 没取到，原因是中文可操作文案
        void failed(const QString &reason);

    private:
        /**
         * @brief 给在飞的那一条一个收场
         * @param reason 失败原因；空表示正常收尾（断开信号用）
         * @details 收场只走这一条：成功、超时、太大、读不出，都从这里把信号发出去并清掉在飞的那一份。
         */
        void concludeWithFailure(const QString &reason);

        /// 给在飞的这一条接上信号：数据累积、大小闸门、读不出与完成
        /// @param reply 网络回复
        void watch(QNetworkReply *reply);

        std::unique_ptr<QNetworkAccessManager> m_network;          ///< 网络访问管理器，本类持有
        QNetworkReply                         *m_reply = nullptr;  ///< 在飞的那一条，不拥有
        QByteArray                             m_payload;          ///< 已经收下来的字节
        QUrl                                   m_source;           ///< 在飞这一条的来源地址
        bool                                   m_overSize = false; ///< 是不是撞上了大小上限（收场文案据此分开）
    };

} // namespace GyanisScribe::App
