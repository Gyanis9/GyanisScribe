/**
 * @file LongCaptureHud.h
 * @brief 滚动长截图的控制条：读数与「完成 / 取消」两枚按钮
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

// 必须在全局作用域里声明：写成 `class QLabel *m_readout` 那种嵌套形态会在本命名空间里造出一个
// GyanisScribe::Capture::QLabel，它是不是就指 ::QLabel 取决于「包含到这里时 ::QLabel 有没有被声明过」。
// 主机那份 Qt 6.11 的头链里先声明过所以看不出问题，容器的 6.4 没有——GCC 当场报 invalid use of incomplete type。
class QLabel;

namespace GyanisScribe::Capture
{

    /**
     * @brief 长截图期间浮在桌面上的控制条
     * @details 拼接收的是「同一块区域反复抓帧」，控制条本身绝不能落在那块区域里——
     *          被拍进去就成了永远对不上的接缝。摆在哪儿由 positionFor 算，本类只管画与发信号。
     */
    class LongCaptureHud : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 算控制条该摆在哪儿
         * @param region 正在截的那块区域，全局逻辑坐标
         * @param available 这块屏去掉任务栏的可用区，全局逻辑坐标
         * @param hudSize 控制条自己的尺寸
         * @return 完整落在 available 内、且贴在选区外侧的矩形；四个方向都放不下时贴着可用区右下角
         */
        [[nodiscard]] static QRect positionFor(const QRect &region, const QRect &available, const QSize &hudSize);

        /**
         * @brief 构造控制条
         * @param parent 父窗口，默认无
         * @details 按钮只有文字：这条控制条要躲开选区，而图标供给器得从界面层递进来——
         *          为两枚按钮拉一条跨模块依赖不值当，文字加悬停说明已经说得清。
         */
        explicit LongCaptureHud(QWidget *parent = nullptr);

        /**
         * @brief 析构控制条
         */
        ~LongCaptureHud() override;

        /**
         * @brief 更新读数
         * @param readout 已经接好的高度与这一轮新增行数合起来的文本，由调用方拼好后交进来
         * @details 读数以等宽档位显示：每接一帧都跳一下，比例字体会让整条控制条左右抖。
         */
        void setReadout(const QString &readout);

        /**
         * @brief 控制条自己的建议尺寸
         * @return 摆位前用来算落点的那份尺寸；控件还没上屏时布局量不出真实大小，这里给一个下限
         */
        [[nodiscard]] QSize preferredHudSize() const;

    signals:
        /**
         * @brief 用户要点「完成」：把这张长图交给下一步
         */
        void finished();

        /**
         * @brief 用户要点「取消」：整段扔掉
         */
        void cancelled();

    private:
        QLabel *m_readout = nullptr; ///< 读数那一行，setReadout 写在这里
    };

} // namespace GyanisScribe::Capture
