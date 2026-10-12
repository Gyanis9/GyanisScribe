/**
 * @file AnnotationToolbarBridge.h
 * @brief 工具栏与引擎之间的接线：一处接齐画笔参数、工具切换、撤销重做与按钮态
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/PenParameters.h"

#include <QObject>

namespace GyanisScribe::Annotation
{
    class AnnotationToolbar;
    class IAnnotationEngine;

    /**
     * @brief 标注工具栏与引擎的接线桥
     * @details 标注窗与「贴图上直接标注」两处宿主共用同一份接线：工具栏上十三项样式信号、
     *          工具切换、撤销重做与按钮态回灌都收在这里。两处各接一份的话，将来加第十四项
     *          参数时必有一处漏接——那种漏接不会编译报错，只表现为「这个开关在那边不生效」。
     * @note 桥只管引擎这一侧；复制/保存/贴图/识别四个动作按钮只把意图重新发出来，
     *       按钮按下去要做什么（复制哪张图、收不收工）由宿主决定。
     */
    class AnnotationToolbarBridge : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 接上工具栏与引擎
         * @param toolbar 工具栏，生命周期必须长于本桥
         * @param engine 标注引擎，生命周期必须长于本桥
         * @param parent QObject 父对象，默认无
         * @details 构造时把工具栏的初值整包下发给引擎：第一笔的样式与按钮显示的一致。
         */
        AnnotationToolbarBridge(AnnotationToolbar &toolbar, IAnnotationEngine &engine, QObject *parent = nullptr);

        /**
         * @brief 当前整套画笔参数
         * @return 初值抄自工具栏，之后随每一次改动更新
         */
        [[nodiscard]] const PenParameters &penParameters() const noexcept;

    signals:
        /// 工具栏上的「复制」被点了
        void copyRequested();
        /// 工具栏上的「保存」被点了
        void saveRequested();
        /// 工具栏上的「贴到屏幕上」被点了
        void pinRequested();
        /// 工具栏上的「识别文字」被点了
        void ocrRequested();

    private:
        /// 把整包参数下发给引擎，并把失败记进日志
        void applyPenParameters();

        AnnotationToolbar &m_toolbar;       ///< 工具栏，不拥有
        IAnnotationEngine &m_engine;        ///< 标注引擎，不拥有
        PenParameters      m_penParameters; ///< 画笔参数整套：工具栏下发的都先汇到这里再给引擎
    };

} // namespace GyanisScribe::Annotation
