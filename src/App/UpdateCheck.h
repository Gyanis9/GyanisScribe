/**
 * @file UpdateCheck.h
 * @brief 检查有没有新版本：数据源是本仓的 GitHub Releases
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <string_view>

class QNetworkAccessManager;
class QNetworkReply;

namespace GyanisScribe::App
{

    /// 数据源：本仓的 GitHub Releases。仓址一搬家，这两条要成对改（一个问、一个给用户点）
    inline constexpr std::string_view kReleaseApiUrl  = "https://api.github.com/repos/Gyanis9/GyanisScribe/releases/latest";
    inline constexpr std::string_view kReleasePageUrl = "https://github.com/Gyanis9/GyanisScribe/releases";

    /// 最新一个正式版的摘要：草稿与预发布由 releases/latest 自己排除，这里不再判
    struct UpdateRelease
    {
        QString tagName; ///< 形如 v0.0.2 的标签
        QString pageUrl; ///< 发布页地址，点一下去下载
    };

    /**
     * @brief 解析 GitHub Releases 的 latest 响应
     * @param payload 响应正文（JSON）
     * @return 成功返回标签与发布页；不是对象、缺字段或字段不是字符串时返回 InvalidArgument 并点名缺了什么
     */
    [[nodiscard]] Core::Expected<UpdateRelease> parseLatestRelease(const QByteArray &payload);

    /// 这一次检查该怎么告诉用户
    struct UpdateVerdict
    {
        bool    hasNewer = false; ///< 有没有比本机版本新的正式版
        QString message;          ///< 一行中文状态
        QString link;             ///< 有新版时给发布页；其余情况为空
    };

    /**
     * @brief 拿本机版本与最新版比一比，拼出要说给用户的那一句
     * @param runningVersion 本机版本（QApplication::applicationVersion()）
     * @param release 最新正式版
     * @return 判定与文案；两边版本号有任一个解析不出来时按「没法比」处理，并把原因写进文案
     */
    [[nodiscard]] UpdateVerdict verdictFor(const QString &runningVersion, const UpdateRelease &release);

    /**
     * @brief 去 GitHub 问一次最新正式版
     * @details 只做「查」，不做「换」：下载与替换可执行体要先有签名与原子替换那一套，
     *          那是另一个量级的活；这里把发布页交给用户自己去点。
     *          失败不重试也不弹窗，原因同时进日志与回调——检查更新失败不该打断任何正事。
     */
    class UpdateChecker : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造检查器
         * @param parent Qt 父对象
         */
        explicit UpdateChecker(QObject *parent = nullptr);

        /// 析构：把还在飞的请求收掉
        ~UpdateChecker() override;

        /// 发起一次检查；上一次还没回来时忽略这一次，不排队也不并发
        void check();

        /**
         * @brief 上一次检查还在飞吗
         * @return true 表示请求已发出、结果还没回来
         * @details 界面按它决定按钮还该不该灰着：只凭 check() 里的重入守卫，界面会以为按过了
         */
        [[nodiscard]] bool isRunning() const noexcept;

    signals:
        /// 有结果了：成功与失败都走这一条
        void finished(const GyanisScribe::App::UpdateVerdict &verdict);

    private:
        QNetworkAccessManager  *m_network = nullptr; ///< 网络入口，随本对象析构
        QPointer<QNetworkReply> m_reply;             ///< 正在飞的那一条，重入与析构都要看它
    };

} // namespace GyanisScribe::App
