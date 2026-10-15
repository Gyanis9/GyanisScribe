/**
 * @file RecognitionRunner.h
 * @brief 把一次文字识别放到工作线程上跑，认完把结果送回界面线程
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Ocr/OcrTypes.h"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include <optional>
#include <utility>

namespace GyanisScribe::Core
{
    class IOcrEngine;

    /**
     * @brief 一次识别的载荷：成功带来结果，失败带来原因
     */
    using OcrOutcome = Base::Expected<OcrResult>;

    /**
     * @brief 识别的线程搬运工
     * @details 后端是同步接口，一次要跑几十到上千毫秒；本类把它放到 Qt 的全局线程池上跑，
     *          结果由 QFutureWatcher 送回界面线程，调用方只接信号。
     *          同一时间只跑一条：后端自己带可变状态（Tesseract 的语言句柄在 recognize() 里换），
     *          并发调同一个后端不是线程安全的。
     */
    class RecognitionRunner : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 绑一个识别后端
         * @param engine 后端；本类不拥有它，它必须活到本对象析构之后——工作线程认字用的是它的指针
         * @param parent Qt 父对象，可空
         */
        explicit RecognitionRunner(IOcrEngine &engine, QObject *parent = nullptr);

        /**
         * @brief 析构前先拆回接线，再等手上那条识别跑完
         * @details 不等就是让工作线程在引擎析构之后继续用它；拆线是防止等待期间结果正好落地，
         *          finished 打在正在析构的对象上。
         */
        ~RecognitionRunner() override;

        RecognitionRunner(const RecognitionRunner &)            = delete;
        RecognitionRunner &operator=(const RecognitionRunner &) = delete;

        /**
         * @brief 认一张图
         * @param image 原图，按值带走：QImage 的写只许发生在界面线程，跨线程只带读数
         * @param language 语言标签，取值来自后端的 supportedLanguages()
         * @details 手上一条还没认完时不排队堆叠，只把最新这一条攒着：用户换了语言或重新截了图，
         *          旧那一条的成品已经没有意义。
         */
        void request(const QImage &image, const QString &language);

    signals:
        /**
         * @brief 一次识别认完了
         * @param image 发出请求时那一张，结果里的方框与它同系；不含期间换上的新图
         * @param language 发出请求时那门语言
         * @param result 识别结果，一行都没认出来也走这里
         * @note 两个信号都在界面线程上发，所以载荷按引用带出不需要注册元类型
         */
        void succeeded(const QImage &image, const QString &language, const OcrResult &result);

        /**
         * @brief 一次识别失败了
         * @param error 后端给的中文原因（含下一步怎么做）
         */
        void failed(const Base::Error &error);

    private:
        /// 起一条线程去认，并把这一次的图与语言记下来，结果回来时按原样配走
        void start(const QImage &image, const QString &language);

        /// 把手上的结果发出去；攒着新请求就立刻按新请求再认一次
        void deliver();

        IOcrEngine                               &m_engine;          ///< 识别后端，不拥有
        QFutureWatcher<OcrOutcome>                m_watcher;         ///< 工作线程结果的回接点
        QImage                                    m_runningImage;    ///< 正在认的那一张
        QString                                   m_runningLanguage; ///< 正在认的那一门语言
        std::optional<std::pair<QImage, QString>> m_pending;         ///< 忙时攒下的最新一条，后到的说了算
    };

} // namespace GyanisScribe::Core
