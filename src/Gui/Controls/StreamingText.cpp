#include "Gui/Controls/StreamingText.h"

#include "Gui/Theme/Motion.h"

#include <QPainter>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace
{
    /**
     * @brief 对整张图做多遍可分离盒式模糊
     * @param image 被模糊的图，就地改写；必须是预乘格式，逐通道平均才等价于按透明度加权
     * @param radius 半径，按物理像素计
     * @param passes 盒式模糊的遍数，三遍接近高斯
     * @details 只在建层时调用一次；逐帧重算高斯会把打字动画的帧预算吃光。
     *          越界的采样点按边缘像素复制处理，避免四周出现一圈压暗。
     */
    void boxBlur(QImage &image, const int radius, const int passes)
    {
        if (radius <= 0 || image.isNull())
        {
            return;
        }
        for (int pass = 0; pass < passes; ++pass)
        {
            QImage horizontal = image;
            for (int y = 0; y < image.height(); ++y)
            {
                quint64 r = 0, g = 0, b = 0, a = 0;
                for (int x = -radius; x <= radius; ++x)
                {
                    const int  sx    = std::clamp(x, 0, image.width() - 1);
                    const QRgb pixel = reinterpret_cast<const QRgb *>(image.constScanLine(y))[sx];
                    r += qRed(pixel);
                    g += qGreen(pixel);
                    b += qBlue(pixel);
                    a += qAlpha(pixel);
                }
                const int count = radius * 2 + 1;
                for (int x = 0; x < image.width(); ++x)
                {
                    reinterpret_cast<QRgb *>(horizontal.scanLine(y))[x] = qRgba(int(r / count), int(g / count), int(b / count), int(a / count));
                    // 滑动窗口：先写当前和，再移出左端、移入右端，供下一列使用
                    const int  removeX = std::clamp(x - radius, 0, image.width() - 1);
                    const int  addX    = std::clamp(x + radius + 1, 0, image.width() - 1);
                    const QRgb remove  = reinterpret_cast<const QRgb *>(image.constScanLine(y))[removeX];
                    const QRgb add     = reinterpret_cast<const QRgb *>(image.constScanLine(y))[addX];
                    r -= qRed(remove);
                    g -= qGreen(remove);
                    b -= qBlue(remove);
                    a -= qAlpha(remove);
                    r += qRed(add);
                    g += qGreen(add);
                    b += qBlue(add);
                    a += qAlpha(add);
                }
            }
            QImage vertical = horizontal;
            for (int x = 0; x < horizontal.width(); ++x)
            {
                quint64 r = 0, g = 0, b = 0, a = 0;
                for (int y = -radius; y <= radius; ++y)
                {
                    const int  sy    = std::clamp(y, 0, horizontal.height() - 1);
                    const QRgb pixel = reinterpret_cast<const QRgb *>(horizontal.constScanLine(sy))[x];
                    r += qRed(pixel);
                    g += qGreen(pixel);
                    b += qBlue(pixel);
                    a += qAlpha(pixel);
                }
                const int count = radius * 2 + 1;
                for (int y = 0; y < horizontal.height(); ++y)
                {
                    reinterpret_cast<QRgb *>(vertical.scanLine(y))[x] = qRgba(int(r / count), int(g / count), int(b / count), int(a / count));
                    const int  removeY                                = std::clamp(y - radius, 0, horizontal.height() - 1);
                    const int  addY                                   = std::clamp(y + radius + 1, 0, horizontal.height() - 1);
                    const QRgb remove                                 = reinterpret_cast<const QRgb *>(horizontal.constScanLine(removeY))[x];
                    const QRgb add                                    = reinterpret_cast<const QRgb *>(horizontal.constScanLine(addY))[x];
                    r -= qRed(remove);
                    g -= qGreen(remove);
                    b -= qBlue(remove);
                    a -= qAlpha(remove);
                    r += qRed(add);
                    g += qGreen(add);
                    b += qBlue(add);
                    a += qAlpha(add);
                }
            }
            image = vertical;
        }
    }
} // namespace

namespace GyanisScribe::Gui
{
    qreal StreamingTimeline::progressFor(const int index, const qint64 elapsedMs) const
    {
        if (index < 0 || index >= count)
        {
            return 0.0;
        }
        const qint64 start = static_cast<qint64>(index) * staggerMs;
        const qreal  raw   = std::clamp((elapsedMs - start) / static_cast<qreal>(fadeMs), 0.0, 1.0);
        return cubicBezier(0.215, 0.61, 0.355, 1.0, raw);
    }

    StreamingTextOverlay::StreamingTextOverlay(QWidget *parent, QString text, QFont font, QColor textColor, QColor backgroundColor) :
        QWidget(parent), m_text(std::move(text)), m_font(std::move(font)), m_textColor(std::move(textColor)), m_background(std::move(backgroundColor))
    {
        // 覆盖层只是画面：鼠标事件要穿透到下面的可选标签，自己也不该进焦点链
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        m_timer.setInterval(16);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
    }

    void StreamingTextOverlay::start()
    {
        if (m_finished)
        {
            return;
        }
        if (!animationsEnabled())
        {
            finish();
            return;
        }
        if (m_glyphs.empty())
        {
            // 几何恰好等于控件默认尺寸时 setGeometry 不发 resizeEvent，这里补建一次
            buildLayers();
        }
        if (m_glyphs.empty())
        {
            finish();
            return;
        }
        m_clock.start();
        m_timer.start();
    }

    void StreamingTextOverlay::resizeEvent(QResizeEvent *event)
    {
        QWidget::resizeEvent(event);
        buildLayers();
    }

    void StreamingTextOverlay::tick()
    {
        if (m_finished)
        {
            return;
        }
        // 上一帧已把最后一帧画出来，这一帧收工：与最后一帧同帧隐藏时，那一帧可能还没被合成上屏
        if (m_elapsedMs >= totalDurationMs())
        {
            finish();
            return;
        }
        // 按真实时钟取经过时间，固定 +16ms 累积会在掉帧时越走越慢；到头就停在最后一帧
        m_elapsedMs = std::min<qint64>(m_clock.elapsed(), totalDurationMs());
        update();
    }

    void StreamingTextOverlay::advanceForTesting(const qint64 milliseconds)
    {
        if (m_finished)
        {
            return;
        }
        // 与 tick() 同一拍：时间轴到头的那一帧先画出来，下一次推进才收工
        if (m_elapsedMs >= totalDurationMs())
        {
            finish();
            return;
        }
        m_elapsedMs = std::min<qint64>(m_elapsedMs + std::max<qint64>(0, milliseconds), totalDurationMs());
        update();
    }

    qint64 StreamingTextOverlay::totalDurationMs() const noexcept
    {
        return m_timeline.totalDurationMs();
    }

    bool StreamingTextOverlay::isFinished() const noexcept
    {
        return m_finished;
    }

    int StreamingTextOverlay::paintCount() const noexcept
    {
        return m_paintCount;
    }

    void StreamingTextOverlay::buildLayers()
    {
        m_glyphs.clear();
        if (m_text.isEmpty() || width() <= 0)
        {
            return;
        }

        // 与 QLabel 同一套断行口径，覆盖层的字位置才对得上收尾后露出的原生文字
        QTextLayout layout{m_text, m_font};
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal y = 0.0;
        while (true)
        {
            QTextLine line = layout.createLine();
            if (!line.isValid())
            {
                break;
            }
            line.setLineWidth(width());
            line.setPosition(QPointF(0.0, y));
            for (int index = line.textStart(); index < line.textStart() + line.textLength(); ++index)
            {
                // 空格不参与逐字动画：给它排一个裁剪矩形只会让停顿看起来像卡顿
                if (m_text.at(index).isSpace())
                {
                    continue;
                }
                const qreal x0 = line.cursorToX(index);
                const qreal x1 = line.cursorToX(index + 1);
                if (x1 > x0)
                {
                    m_glyphs.push_back({static_cast<int>(m_glyphs.size()), QRectF(x0, line.y(), x1 - x0, line.height())});
                }
            }
            y += line.height();
        }
        layout.endLayout();
        m_timeline.count = static_cast<int>(m_glyphs.size());
        if (m_glyphs.empty() || height() <= 0)
        {
            return;
        }

        const qreal dpr = devicePixelRatioF();
        const QSize deviceSize{std::max(1, qRound(width() * dpr)), std::max(1, qRound(height() * dpr))};
        // 预乘格式：模糊时逐通道平均才按透明度加权，透明像素不会把语义色拖向黑
        m_sharp = QImage{deviceSize, QImage::Format_ARGB32_Premultiplied};
        m_sharp.fill(Qt::transparent);
        {
            QPainter painter{&m_sharp};
            painter.scale(dpr, dpr);
            painter.setFont(m_font);
            painter.setPen(m_textColor);
            layout.draw(&painter, QPointF(0.0, 0.0));
        }
        // 模糊层只在建层时算一次；逐帧重算会把 16ms 的帧预算吃掉
        m_blurred = m_sharp;
        boxBlur(m_blurred, std::max(1, qRound(6.0 * dpr)), 3);
    }

    void StreamingTextOverlay::paintEvent(QPaintEvent *)
    {
        ++m_paintCount;
        QPainter painter{this};
        // 先盖掉原生标签：动画期间不该看见整齐的整句，收尾时再一起露出来
        painter.fillRect(rect(), m_background);
        if (m_glyphs.empty())
        {
            return;
        }
        const qreal dpr = devicePixelRatioF();
        for (const auto &glyph: m_glyphs)
        {
            const qreal progress = m_timeline.progressFor(glyph.index, m_elapsedMs);
            if (progress <= 0.0)
            {
                continue;
            }
            // 外扩 1/2 像素留出模糊外溢，否则 6px 模糊会在字边被裁成硬边
            const QRectF target = glyph.rect.adjusted(-1.0, -2.0, 1.0, 2.0);
            const QRectF source{target.x() * dpr, target.y() * dpr, target.width() * dpr, target.height() * dpr};
            painter.save();
            painter.setClipRect(target);
            if (progress < 1.0)
            {
                // 模糊层按「离清晰还差多少」淡出，进度到 1 时完全换清晰层
                painter.setOpacity(static_cast<qreal>(1.0 - progress) * 0.85);
                painter.drawImage(target, m_blurred, source);
            }
            painter.setOpacity(progress);
            painter.drawImage(target, m_sharp, source);
            painter.restore();
        }
    }

    void StreamingTextOverlay::finish()
    {
        if (m_finished)
        {
            return;
        }
        m_finished = true;
        m_timer.stop();
        // 隐藏即让开原生标签；销毁交给调用方的 finished 处理
        hide();
        emit finished();
    }
} // namespace GyanisScribe::Gui
