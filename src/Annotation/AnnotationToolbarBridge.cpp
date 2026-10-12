#include "Annotation/AnnotationToolbarBridge.h"

#include "Annotation/AnnotationToolbar.h"
#include "Annotation/IAnnotationEngine.h"
#include "Core/Logger.h"

#include <format>

namespace GyanisScribe::Annotation
{
    AnnotationToolbarBridge::AnnotationToolbarBridge(AnnotationToolbar &toolbar, IAnnotationEngine &engine, QObject *parent) : QObject(parent), m_toolbar(toolbar), m_engine(engine)
    {
        // 起点参数整套照工具栏的初值抄一遍：十七处必须与工具栏同解，否则第一笔的样式与按钮显示的不一致
        m_penParameters = PenParameters{.color                = toolbar.currentColor(),
                                        .lineWidth            = toolbar.currentWidth(),
                                        .fontSize             = toolbar.currentFontSize(),
                                        .fillStyle            = toolbar.currentFillStyle(),
                                        .lineStyle            = toolbar.currentLineStyle(),
                                        .roundedRect          = toolbar.currentRoundedRect(),
                                        .doubleHeaded         = toolbar.currentDoubleHeaded(),
                                        .elliptical           = toolbar.currentElliptical(),
                                        .mosaicBlockSize      = toolbar.currentMosaicBlockSize(),
                                        .highlightAlpha       = toolbar.currentHighlightAlpha(),
                                        .highlightWidthFactor = toolbar.currentHighlightWidthFactor(),
                                        .arrowHeadStyle       = toolbar.currentArrowHeadStyle(),
                                        .arrowHeadScale       = toolbar.currentArrowHeadScale(),
                                        .fontFamily           = toolbar.currentFontFamily(),
                                        .textBold             = toolbar.currentTextBold(),
                                        .textItalic           = toolbar.currentTextItalic(),
                                        .textOutline          = toolbar.currentTextOutline(),
                                        .textBackground       = toolbar.currentTextBackground(),
                                        .textStrokeColorStep  = toolbar.currentTextStrokeColorStep()};

        QObject::connect(&toolbar, &AnnotationToolbar::toolChanged, this,
                         [this](const ToolKind kind)
                         {
                             if (auto switched = m_engine.setActiveTool(kind); !switched)
                             {
                                 Core::Logger::warning(std::format("切工具没成：{}", switched.error().message));
                             }
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::colorChanged, this,
                         [this](const QColor &color)
                         {
                             m_penParameters.color = color;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::widthChanged, this,
                         [this](const int width)
                         {
                             m_penParameters.lineWidth = width;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::fontSizeChanged, this,
                         [this](const int size)
                         {
                             m_penParameters.fontSize = size;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::fillStyleChanged, this,
                         [this](const int style)
                         {
                             m_penParameters.fillStyle = style;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::roundedRectChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.roundedRect = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::lineStyleChanged, this,
                         [this](const int style)
                         {
                             m_penParameters.lineStyle = style;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::doubleHeadedChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.doubleHeaded = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::ellipticalChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.elliptical = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::mosaicBlockSizeChanged, this,
                         [this](const int blockSize)
                         {
                             m_penParameters.mosaicBlockSize = blockSize;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::highlightAlphaChanged, this,
                         [this](const int alpha)
                         {
                             m_penParameters.highlightAlpha = alpha;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::highlightWidthFactorChanged, this,
                         [this](const int factor)
                         {
                             m_penParameters.highlightWidthFactor = factor;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::arrowHeadStyleChanged, this,
                         [this](const int style)
                         {
                             m_penParameters.arrowHeadStyle = style;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::arrowHeadScaleChanged, this,
                         [this](const qreal scale)
                         {
                             m_penParameters.arrowHeadScale = scale;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::fontFamilyChanged, this,
                         [this](const QString &family)
                         {
                             m_penParameters.fontFamily = family;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::textBoldChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.textBold = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::textItalicChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.textItalic = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::textOutlineChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.textOutline = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::textBackgroundChanged, this,
                         [this](const bool enabled)
                         {
                             m_penParameters.textBackground = enabled;
                             applyPenParameters();
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::textStrokeColorStepChanged, this,
                         [this](const int step)
                         {
                             m_penParameters.textStrokeColorStep = step;
                             applyPenParameters();
                         });
        // 计数组不是画笔样式（不进 PenParameters）：它是序号工具自己的计数状态，直接交给引擎
        QObject::connect(&toolbar, &AnnotationToolbar::numberGroupChanged, this,
                         [this](const int group)
                         {
                             if (auto applied = m_engine.setNumberGroup(group); !applied)
                             {
                                 Core::Logger::warning(std::format("切计数组没成：{}", applied.error().message));
                             }
                         });

        QObject::connect(&toolbar, &AnnotationToolbar::undoClicked, this,
                         [this]
                         {
                             if (auto undone = m_engine.undo(); !undone)
                             {
                                 Core::Logger::info(std::format("撤销没做成：{}", undone.error().message));
                             }
                         });
        QObject::connect(&toolbar, &AnnotationToolbar::redoClicked, this,
                         [this]
                         {
                             if (auto redone = m_engine.redo(); !redone)
                             {
                                 Core::Logger::info(std::format("重做没做成：{}", redone.error().message));
                             }
                         });

        // 四个动作按钮只把意图转出来：按下去做什么由宿主决定，桥不替它挑默认
        QObject::connect(&toolbar, &AnnotationToolbar::copyClicked, this, &AnnotationToolbarBridge::copyRequested);
        QObject::connect(&toolbar, &AnnotationToolbar::saveClicked, this, &AnnotationToolbarBridge::saveRequested);
        QObject::connect(&toolbar, &AnnotationToolbar::pinClicked, this, &AnnotationToolbarBridge::pinRequested);
        QObject::connect(&toolbar, &AnnotationToolbar::ocrClicked, this, &AnnotationToolbarBridge::ocrRequested);
        QObject::connect(&toolbar, &AnnotationToolbar::scanClicked, this, &AnnotationToolbarBridge::scanRequested);

        // 引擎的栈深或工具变了就刷按钮：让用户看见「还能不能撤」而不是点了才知道没得撤；
        // 快捷键切的工具也从这里回灌到按钮上，两处永远同解
        QObject::connect(&engine, &IAnnotationEngine::canvasChanged, this,
                         [this]
                         {
                             m_toolbar.setUndoRedoAvailable(m_engine.canUndo(), m_engine.canRedo());
                             m_toolbar.setActiveTool(m_engine.activeTool());
                         });

        applyPenParameters();
    }

    const PenParameters &AnnotationToolbarBridge::penParameters() const noexcept
    {
        return m_penParameters;
    }

    void AnnotationToolbarBridge::applyPenParameters()
    {
        if (auto applied = m_engine.setPenParameters(m_penParameters); !applied)
        {
            Core::Logger::warning(std::format("画笔参数没下发：{}", applied.error().message));
        }
    }

} // namespace GyanisScribe::Annotation
