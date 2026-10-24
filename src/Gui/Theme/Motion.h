/**
 * @file Motion.h
 * @brief 动效原语：弹簧解析解、稳定时长、CSS 三次贝塞尔与可步进的弹簧值
 * @author Gyanis
 * @date 2026-10-24
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QObject>
#include <QTimer>
#include <QtGlobal>

#include <optional>

namespace GyanisScribe::Gui
{
    /**
     * @brief 弹簧参数
     * @details 动画按质量归一化；质量非正时按 1 处理，阻尼比与角频率由三个值共同算出。
     */
    struct SpringSpec
    {
        qreal stiffness = 1200.0; ///< 刚度，越大回稳越快
        qreal damping   = 80.0;   ///< 阻尼，小于临界值时过冲
        qreal mass      = 1.0;    ///< 质量，只影响换算出的角频率
    };

    /// 贴图吸附与贴边收展：过阻尼，单调逼近目标、不越过
    inline constexpr SpringSpec kSnapSpring{1200.0, 80.0, 1.0};
    /// 对勾与 Toast 成功图标：欠阻尼，先过冲约 19.5% 再回稳
    inline constexpr SpringSpec kPopSpring{380.0, 18.0, 1.0};
    /// 弹簧动画的帧间隔，与 60Hz 刷新对齐
    inline constexpr int kSpringFrameMs = 16;

    /**
     * @brief 弹簧位移响应
     * @param spec 弹簧参数
     * @param elapsedSeconds 从动画起点算起的秒数，负值按 0 处理
     * @return 归一化进度；起点为 0，过阻尼不超过 1，欠阻尼会短暂越过 1
     */
    [[nodiscard]] qreal springProgress(const SpringSpec &spec, qreal elapsedSeconds);

    /**
     * @brief 弹簧进入稳定带所需的毫秒数
     * @param spec 弹簧参数
     * @param band 稳定带半宽，按 |1 - 进度| 判定
     * @return 动画总时长，按帧对齐且不小于 80ms
     * @note 只扫描前 2 秒；更慢的弹簧按扫描终点计。
     */
    [[nodiscard]] int springSettleMs(const SpringSpec &spec, qreal band = 0.01);

    /**
     * @brief 求 CSS cubic-bezier() 曲线的纵坐标
     * @details 四个控制点为 (0,0)、(x1,y1)、(x2,y2)、(1,1)；先按牛顿迭代把横轴进度
     *          反解成曲线参数，再取纵坐标。
     * @param x1 第一个控制点的横坐标
     * @param y1 第一个控制点的纵坐标
     * @param x2 第二个控制点的横坐标
     * @param y2 第二个控制点的纵坐标
     * @param progress 横轴进度，超出 [0,1] 时夹到端点
     * @return 与 progress 对应的纵轴进度
     */
    [[nodiscard]] qreal cubicBezier(qreal x1, qreal y1, qreal x2, qreal y2, qreal progress);

    /**
     * @brief 设置全局动画开关
     * @param enabled false 时新的 animateTo 直接落到目标
     * @note 生产侧只调本函数，测试侧只调 setAnimationsEnabledForTesting；两者写同一份状态。
     */
    void setAnimationsEnabled(bool enabled);

    /**
     * @brief 读取全局动画开关
     * @return 默认允许动画；被覆盖时返回覆盖值
     */
    [[nodiscard]] bool animationsEnabled();

    /**
     * @brief 覆盖全局动画开关，供测试注入
     * @param overrideValue 覆盖值；std::nullopt 恢复默认
     */
    void setAnimationsEnabledForTesting(std::optional<bool> overrideValue);

    /**
     * @brief 带弹簧插值的标量值
     * @details 正常运行时由 QTimer 按 kSpringFrameMs 推进；测试可用 advanceBy 直接步进，
     *          不依赖真实时钟。没有 QCoreApplication 时不启动定时器，只保留可步进状态。
     */
    class SpringValue final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(qreal value READ value WRITE setValue NOTIFY valueChanged)

    public:
        /**
         * @brief 构造弹簧值，起点为 0
         * @param parent Qt 父对象
         */
        explicit SpringValue(QObject *parent = nullptr);

        /**
         * @brief 设置后续动画使用的弹簧参数
         * @param spec 弹簧参数
         */
        void setSpec(SpringSpec spec);

        /**
         * @brief 设置本对象的动画开关
         * @param enabled false 时进行中的动画立即落到目标
         */
        void setAnimationsEnabled(bool enabled);

        /**
         * @brief 读取本对象与全局开关的合成结果
         * @return 两者都允许时才为 true
         */
        [[nodiscard]] bool animationsEnabled() const noexcept;

        /// 当前值
        [[nodiscard]] qreal value() const noexcept;

        /**
         * @brief 属性写入口：立即落值并停止进行中的动画
         * @param value 目标值
         */
        void setValue(qreal value);

        /**
         * @brief 不带动画地落值
         * @param value 目标值
         */
        void setImmediate(qreal value);

        /**
         * @brief 从当前值弹性过渡到目标
         * @param target 目标值
         */
        void animateTo(qreal target);

        /// 停在当前值并停止定时器
        void stop();

        /// 是否还有未走完的动画
        [[nodiscard]] bool isRunning() const noexcept;

        /**
         * @brief 手动推进动画
         * @param milliseconds 本次推进的毫秒数，负值按 0 处理
         * @note 只在动画进行中生效；到总时长后精确落到目标并停止。
         */
        void advanceBy(qint64 milliseconds);

    signals:
        /// 值已变化
        void valueChanged(qreal value);

    private:
        /// 定时器每帧推进一次
        void tick();

        SpringSpec m_spec{kSnapSpring};         ///< 当前弹簧参数
        qreal      m_start             = 0.0;   ///< 本次动画的起点
        qreal      m_target            = 0.0;   ///< 本次动画的目标
        qreal      m_value             = 0.0;   ///< 当前值
        qint64     m_elapsedMs         = 0;     ///< 本次动画已推进的毫秒数
        int        m_durationMs        = 0;     ///< 本次动画的总时长
        bool       m_running           = false; ///< 是否有动画在进行
        bool       m_animationsEnabled = true;  ///< 本对象的动画开关
        QTimer     m_timer;                     ///< 帧定时器
    };
} // namespace GyanisScribe::Gui
