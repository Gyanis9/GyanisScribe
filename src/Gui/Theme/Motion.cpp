#include "Gui/Theme/Motion.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace
{
    /// 全局动画开关；生产设定与测试覆盖写同一份状态，std::nullopt 表示默认允许
    std::optional<bool> g_testOverride;

    /**
     * @brief 弹簧的归一化位移响应
     * @details 按阻尼比分为欠阻尼、临界阻尼、过阻尼三支解析解；质量非正时按 1 处理。
     * @param spec 弹簧参数
     * @param t 已过秒数，调用方保证非负
     * @return 归一化进度
     */
    [[nodiscard]] qreal responseAt(const GyanisScribe::Gui::SpringSpec &spec, const qreal t)
    {
        const qreal m  = spec.mass <= 0.0 ? 1.0 : spec.mass;
        const qreal wn = std::sqrt(spec.stiffness / m);
        const qreal z  = spec.damping / (2.0 * std::sqrt(spec.stiffness * m));

        // 欠阻尼：带指数衰减的振荡，会越过 1 再回落
        if (z < 1.0)
        {
            const qreal wd = wn * std::sqrt(1.0 - z * z);
            return 1.0 - std::exp(-z * wn * t) * (std::cos(wd * t) + (z * wn / wd) * std::sin(wd * t));
        }
        // 临界阻尼：无振荡，单调逼近 1
        if (std::abs(z - 1.0) < 1e-6)
        {
            return 1.0 - std::exp(-wn * t) * (1.0 + wn * t);
        }

        // 过阻尼：两个实根，按初值 y(0)=0 与 y'(0)=0 解出两个系数
        const qreal root = wn * std::sqrt(z * z - 1.0);
        const qreal r1   = -z * wn + root;
        const qreal r2   = -z * wn - root;
        const qreal a    = -r2 / (r1 - r2);
        const qreal b    = r1 / (r1 - r2);
        return 1.0 - a * std::exp(r1 * t) - b * std::exp(r2 * t);
    }

    /// 三次贝塞尔在参数 t 处的一个分量，控制点为 0、c1、c2、1
    [[nodiscard]] qreal bezierComponent(const qreal c1, const qreal c2, const qreal t)
    {
        const qreal u = 1.0 - t;
        return 3.0 * u * u * t * c1 + 3.0 * u * t * t * c2 + t * t * t;
    }

    /// 上述分量对 t 的导数，牛顿迭代用它反解横轴
    [[nodiscard]] qreal bezierSlope(const qreal c1, const qreal c2, const qreal t)
    {
        const qreal u = 1.0 - t;
        return 3.0 * u * u * c1 + 6.0 * u * t * (c2 - c1) + 3.0 * t * t * (1.0 - c2);
    }
} // namespace

namespace GyanisScribe::Gui
{
    qreal springProgress(const SpringSpec &spec, const qreal elapsedSeconds)
    {
        // 起点之前没有位移，负时间一律按 0 处理
        return responseAt(spec, std::max(0.0, elapsedSeconds));
    }

    int springSettleMs(const SpringSpec &spec, const qreal band)
    {
        // 逐毫秒找最后一次落在稳定带外的时刻；总时长要覆盖到它之后，不能提前跳目标
        int lastOutside = 0;
        for (int ms = 0; ms <= 2000; ++ms)
        {
            if (std::abs(1.0 - springProgress(spec, ms / 1000.0)) >= band)
            {
                lastOutside = ms;
            }
        }
        // 再加一帧让动画走到越界时刻之后；80ms 下限避免极硬弹簧的过渡短到看不见
        return std::max(80, lastOutside + kSpringFrameMs);
    }

    qreal cubicBezier(const qreal x1, const qreal y1, const qreal x2, const qreal y2, const qreal progress)
    {
        const qreal x = std::clamp(progress, 0.0, 1.0);
        qreal       t = x;
        // 横轴控制点落在 [0,1] 内时横轴单调，牛顿迭代 8 次足以收敛到像素级
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            const qreal error = bezierComponent(x1, x2, t) - x;
            const qreal slope = bezierSlope(x1, x2, t);
            // 斜率接近零时不再迭代，避免除零把参数打成非数
            if (std::abs(slope) < 1e-6)
            {
                break;
            }
            t -= error / slope;
        }
        // 迭代结果可能略出界，取纵坐标前夹回曲线参数区间
        return bezierComponent(y1, y2, std::clamp(t, 0.0, 1.0));
    }

    void setAnimationsEnabled(const bool enabled)
    {
        // 与测试覆盖值共用同一份状态：生产侧的设定本身也是一次覆盖
        g_testOverride = enabled;
    }

    bool animationsEnabled()
    {
        return g_testOverride.value_or(true);
    }

    void setAnimationsEnabledForTesting(const std::optional<bool> overrideValue)
    {
        g_testOverride = overrideValue;
    }

    SpringValue::SpringValue(QObject *parent) : QObject(parent)
    {
        // 精确定时器让 16ms 帧间隔不被粗定时器拉长，弹簧的采样才跟得上曲线
        m_timer.setInterval(kSpringFrameMs);
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
    }

    void SpringValue::setSpec(const SpringSpec spec)
    {
        m_spec = spec;
    }

    void SpringValue::setAnimationsEnabled(const bool enabled)
    {
        m_animationsEnabled = enabled;
        // 关闭动画时进行中的过渡立即落到目标，不做逐帧追赶
        if (!enabled && m_running)
        {
            setImmediate(m_target);
        }
    }

    bool SpringValue::animationsEnabled() const noexcept
    {
        return m_animationsEnabled && Gui::animationsEnabled();
    }

    qreal SpringValue::value() const noexcept
    {
        return m_value;
    }

    void SpringValue::setValue(const qreal value)
    {
        stop();
        setImmediate(value);
    }

    void SpringValue::setImmediate(const qreal value)
    {
        m_value  = value;
        m_target = value;
        emit valueChanged(m_value);
    }

    void SpringValue::animateTo(const qreal target)
    {
        // 动画关闭或已在目标值时不做过渡，直接落值
        if (!animationsEnabled() || qFuzzyCompare(m_value + 1.0, target + 1.0))
        {
            setImmediate(target);
            return;
        }
        m_start      = m_value;
        m_target     = target;
        m_elapsedMs  = 0;
        m_durationMs = springSettleMs(m_spec);
        m_running    = true;
        // 没有 QCoreApplication 时启动 QTimer 会报警告：测试靠 advanceBy 步进，
        // 此时只保留动画状态，不启动定时器。
        if (QCoreApplication::instance() != nullptr)
        {
            m_timer.start();
        }
    }

    void SpringValue::stop()
    {
        m_running = false;
        m_timer.stop();
    }

    bool SpringValue::isRunning() const noexcept
    {
        return m_running;
    }

    void SpringValue::advanceBy(const qint64 milliseconds)
    {
        if (!m_running)
        {
            return;
        }
        m_elapsedMs += std::max<qint64>(0, milliseconds);
        // 到总时长精确落到目标：解析解在稳定带末端只是接近 1，不能靠采样收尾
        if (m_elapsedMs >= m_durationMs)
        {
            m_value   = m_target;
            m_running = false;
            m_timer.stop();
        } else
        {
            // 起点到目标按弹簧进度映射，欠阻尼的过冲因此也作用在目标值上
            const qreal progress = springProgress(m_spec, m_elapsedMs / 1000.0);
            m_value              = m_start + (m_target - m_start) * progress;
        }
        emit valueChanged(m_value);
    }

    void SpringValue::tick()
    {
        advanceBy(kSpringFrameMs);
    }
} // namespace GyanisScribe::Gui
