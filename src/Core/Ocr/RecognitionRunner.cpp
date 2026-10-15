#include "Core/Ocr/RecognitionRunner.h"

#include "Core/Ocr/IOcrEngine.h"

#include <QtConcurrent/QtConcurrentRun>

namespace GyanisScribe::Core
{
    RecognitionRunner::RecognitionRunner(IOcrEngine &engine, QObject *parent) : QObject(parent), m_engine(engine)
    {
        QObject::connect(&m_watcher, &QFutureWatcher<OcrOutcome>::finished, this, [this] { deliver(); });
    }

    RecognitionRunner::~RecognitionRunner()
    {
        // 先拆回接的线再等：等待期间结果落地的话，finished 会打在正在析构的对象上
        QObject::disconnect(&m_watcher, nullptr, this, nullptr);
        m_watcher.waitForFinished();
    }

    void RecognitionRunner::request(const QImage &image, const QString &language)
    {
        if (m_watcher.isRunning())
        {
            m_pending = std::make_pair(image, language);
            return;
        }
        start(image, language);
    }

    void RecognitionRunner::start(const QImage &image, const QString &language)
    {
        m_runningImage    = image;
        m_runningLanguage = language;
        // 引擎指针进闭包：recognize() 的整个调用都发生在线程池那一边，界面线程一份 QImage 都不碰
        m_watcher.setFuture(QtConcurrent::run([engine = &m_engine, image, language] { return engine->recognize(image, language); }));
    }

    void RecognitionRunner::deliver()
    {
        const OcrOutcome outcome = m_watcher.result();
        if (outcome)
        {
            emit succeeded(m_runningImage, m_runningLanguage, *outcome);
        } else
        {
            emit failed(outcome.error());
        }

        // 攒着的那一条排在结果之后：界面先看到这一次的成品，再被下一次的结果换掉
        if (m_pending)
        {
            const auto pending = *std::move(m_pending);
            m_pending.reset();
            start(pending.first, pending.second);
        }
    }

} // namespace GyanisScribe::Core
