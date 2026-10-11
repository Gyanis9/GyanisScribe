#include "Annotation/AnnotationEngine.h"

#include "Annotation/ArrowTool.h"
#include "Annotation/EllipseTool.h"
#include "Annotation/EraserTool.h"
#include "Annotation/HighlightTool.h"
#include "Annotation/LineTool.h"
#include "Annotation/BlurTool.h"
#include "Annotation/MosaicTool.h"
#include "Annotation/NumberTool.h"
#include "Annotation/PenTool.h"
#include "Annotation/RectangleTool.h"
#include "Annotation/TextTool.h"
#include "Core/Text.h"

#include <QImage>
#include <QUndoCommand>

#include <format>

namespace GyanisScribe::Annotation
{
    namespace
    {
        /// 改样式命令的合并标识，取值只在本文件内用来分派：平移那一格另有一个编号
        constexpr int kRestyleCommandTypeId = 1;

        /// 平移命令的合并标识：与样式各自一个编号，改线宽与挪位置这两趟手势不许互相并成一步
        constexpr int kMoveCommandTypeId = 2;

        /**
         * @brief 插入一条图形
         * @details 撤销栈里的命令都只碰工具的存储接口，不碰交互态：
         *          撤销不该把在途图形也带回来。
         */
        class InsertShapeCommand : public QUndoCommand
        {
        public:
            InsertShapeCommand(AnnotationTool *tool, int index, const AnnotationShape &shape, QUndoCommand *parent = nullptr) :
                QUndoCommand(parent), m_tool(tool), m_index(index), m_shape(shape)
            {
            }

            void redo() override
            {
                m_tool->insertShape(m_index, m_shape);
            }

            void undo() override
            {
                static_cast<void>(m_tool->takeShapeAt(m_index));
            }

        private:
            AnnotationTool *m_tool;  ///< 目标工具，所有权在引擎
            int             m_index; ///< 插入位置
            AnnotationShape m_shape; ///< 图形内容
        };

        /**
         * @brief 取走一条图形（删除）
         * @details 撤销时按原索引把内容放回去，索引与内容成对存下来才谈得上原样还原。
         */
        class RemoveShapeCommand : public QUndoCommand
        {
        public:
            RemoveShapeCommand(AnnotationTool *tool, int index, const AnnotationShape &shape, QUndoCommand *parent = nullptr) :
                QUndoCommand(parent), m_tool(tool), m_index(index), m_shape(shape)
            {
            }

            void redo() override
            {
                static_cast<void>(m_tool->takeShapeAt(m_index));
            }

            void undo() override
            {
                m_tool->insertShape(m_index, m_shape);
            }

        private:
            AnnotationTool *m_tool;  ///< 目标工具
            int             m_index; ///< 取走的位置
            AnnotationShape m_shape; ///< 被取走的图形，撤销时要原样放回去
        };

        /**
         * @brief 平移一条图形
         * @details 撤销用等量反向位移：记下位移本身，而不是记下移动前后的两个位置。
         *          同一条图形的连续平移并成一步：拖一趟会发几十次位移，方向键逐格也能连按好几次，
         *          不并的话撤销一次只退一格，用户只能一直按着 Ctrl+Z 往回跑。
         */
        class MoveShapeCommand : public QUndoCommand
        {
        public:
            MoveShapeCommand(AnnotationTool *tool, int index, const QPoint &delta, QUndoCommand *parent = nullptr) :
                QUndoCommand(parent), m_tool(tool), m_index(index), m_delta(delta)
            {
            }

            void redo() override
            {
                static_cast<void>(m_tool->translateShapeBy(m_index, m_delta));
            }

            void undo() override
            {
                static_cast<void>(m_tool->translateShapeBy(m_index, -m_delta));
            }

            [[nodiscard]] int id() const override
            {
                return kMoveCommandTypeId;
            }

            bool mergeWith(const QUndoCommand *other) override
            {
                if (other->id() != kMoveCommandTypeId)
                {
                    return false;
                }
                const auto &next = *static_cast<const MoveShapeCommand *>(other);
                // 只并同一条图形的位移：换了工具或换了序号，那就是另一趟意图
                if (next.m_tool != m_tool || next.m_index != m_index)
                {
                    return false;
                }

                // Qt 的 QUndoStack::push() 先跑新命令的 redo() 再试合并，位移已经落到图形上：
                // 这里只把两段累加成一个总位移，撤销取的是反向总量，回到这一趟开始之前
                m_delta += next.m_delta;
                return true;
            }

        private:
            AnnotationTool *m_tool;  ///< 目标工具
            int             m_index; ///< 图形索引
            QPoint          m_delta; ///< 位移量
        };

        /**
         * @brief 改一条图形的样式
         * @details 同一条图形的连续改动并成一步：线宽滑块拖一趟会发好几次值，撤销一次就该回到
         *          抬手之前，而不是让用户按好几下 Ctrl+Z。合并靠 id() 相同才会被栈尝试。
         */
        class RestyleShapeCommand : public QUndoCommand
        {
        public:
            RestyleShapeCommand(AnnotationTool *tool, int index, const AnnotationShape &before, const QColor &color, int lineWidth, int fontSize) :
                m_tool(tool), m_index(index), m_before(before), m_color(color), m_lineWidth(lineWidth), m_fontSize(fontSize)
            {
            }

            void redo() override
            {
                static_cast<void>(m_tool->restyleShapeAt(m_index, m_color, m_lineWidth, m_fontSize));
            }

            void undo() override
            {
                static_cast<void>(m_tool->restyleShapeAt(m_index, m_before.color, m_before.lineWidth, m_before.fontSize));
            }

            [[nodiscard]] int id() const override
            {
                return kRestyleCommandTypeId;
            }

            bool mergeWith(const QUndoCommand *other) override
            {
                if (other->id() != kRestyleCommandTypeId)
                {
                    return false;
                }
                const auto &next = *static_cast<const RestyleShapeCommand *>(other);
                if (next.m_tool != m_tool || next.m_index != m_index)
                {
                    return false;
                }

                // m_before 保持第一次那一份：撤销要回到这一轮手势开始之前，而不是上一个中间档
                m_color     = next.m_color;
                m_lineWidth = next.m_lineWidth;
                m_fontSize  = next.m_fontSize;
                return true;
            }

        private:
            AnnotationTool *m_tool;      ///< 目标工具，所有权在引擎
            int             m_index;     ///< 图形索引
            AnnotationShape m_before;    ///< 改动前的图形，撤销只取它那三个样式字段
            QColor          m_color;     ///< 目标颜色
            int             m_lineWidth; ///< 目标线宽
            int             m_fontSize;  ///< 目标字号
        };

        /**
         * @brief 改一条文字图形的内容
         * @details 不合并：行内输入框一次回车就是一次「说定了」，连着两次改的是两次意图，
         *          合成一步会让用户按一次 Ctrl+Z 跳过头一次改动。
         */
        class SetTextCommand : public QUndoCommand
        {
        public:
            SetTextCommand(AnnotationTool *tool, int index, const QString &before, const QString &text, QUndoCommand *parent = nullptr) :
                QUndoCommand(parent), m_tool(tool), m_index(index), m_before(before), m_text(text)
            {
            }

            void redo() override
            {
                static_cast<void>(m_tool->setTextAt(m_index, m_text));
            }

            void undo() override
            {
                static_cast<void>(m_tool->setTextAt(m_index, m_before));
            }

        private:
            AnnotationTool *m_tool;   ///< 目标工具，所有权在引擎
            int             m_index;  ///< 图形索引
            QString         m_before; ///< 改之前的文字
            QString         m_text;   ///< 目标文字
        };

    } // namespace

    AnnotationEngine::AnnotationEngine(QObject *parent) : IAnnotationEngine(parent)
    {
        m_tools.push_back(std::make_unique<RectangleTool>());
        m_tools.push_back(std::make_unique<EllipseTool>());
        m_tools.push_back(std::make_unique<ArrowTool>());
        m_tools.push_back(std::make_unique<PenTool>());
        m_tools.push_back(std::make_unique<HighlightTool>());
        m_tools.push_back(std::make_unique<MosaicTool>());
        // 新增一档工具只在这里补一行：工具栏按 kToolTable 长出按钮，顺序两边一致
        m_tools.push_back(std::make_unique<BlurTool>());
        m_tools.push_back(std::make_unique<TextTool>());
        m_tools.push_back(std::make_unique<NumberTool>());
        m_tools.push_back(std::make_unique<LineTool>());
        // 橡皮擦登记在最后＝压在全部工具之上：擦的是「画在底图上的一切」，露出原图那一块
        m_tools.push_back(std::make_unique<EraserTool>());

        // 参数一次下发：切工具后笔还保持用户调过的那个粗细，才不会出现「换工具就得重调」
        for (const auto &tool: m_tools)
        {
            tool->setPenColor(m_color);
            tool->setLineWidth(m_lineWidth);
            tool->setFontSize(m_fontSize);
        }

        QObject::connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] { emit canvasChanged(); });
    }

    AnnotationEngine::~AnnotationEngine() = default;

    void AnnotationEngine::loadCanvas(Core::CapturedImage image)
    {
        m_canvas = std::move(image);

        // 换画布等于换一张纸：旧图形既不在新帧上，撤销回去也只会把用户引到错的地方
        m_undoStack.clear();
        m_selection.reset();
        for (const auto &tool: m_tools)
        {
            while (tool->shapeCount() > 0)
            {
                static_cast<void>(tool->takeShapeAt(0));
            }
        }
        emit canvasChanged();
    }

    const Core::CapturedImage &AnnotationEngine::canvas() const noexcept
    {
        return m_canvas;
    }

    QImage AnnotationEngine::exportImage() const
    {
        if (m_canvas.image.isNull())
        {
            return {};
        }

        QImage result = m_canvas.image.copy();
        // 倍比由「物理宽 ÷ 逻辑宽」现算：拿屏幕的 devicePixelRatio 会在缩放窗口后与画布对不上
        const qreal pixelScale = m_canvas.logicalSize.width() > 0 ? static_cast<qreal>(result.width()) / m_canvas.logicalSize.width() : 1.0;

        // 源图取导出前那份底图：烘图过程中 result 在被改，马赛克要的始终是原始像素
        const PaintContext bakeContext{m_canvas.image, pixelScale};
        for (const auto &tool: m_tools)
        {
            tool->applyToImage(result, bakeContext);
        }

        // 倍比要标回导出图：贴图窗口按 image 的 devicePixelRatio 反推逻辑尺寸，
        // 不标就会把高 DPI 屏上截的那一块开成放大 1.5 倍的窗口（跨模块集成用例红在这里抓到的）。
        // 只在不是 1.0 时标：100% 缩放的普通屏幕保持逐字节不变的旧形状
        if (pixelScale != 1.0)
        {
            result.setDevicePixelRatio(pixelScale);
        }
        return result;
    }

    Core::Expected<void> AnnotationEngine::setActiveTool(ToolKind kind)
    {
        auto *target = toolFor(kind);
        if (target == nullptr)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("工具种类 0x{:X} 没有对应的工具：请把新工具登记进构造列表"), static_cast<int>(kind))});
        }

        for (const auto &tool: m_tools)
        {
            tool->setActive(tool.get() == target);
        }
        m_activeTool = kind;
        return {};
    }

    ToolKind AnnotationEngine::activeTool() const noexcept
    {
        return m_activeTool;
    }

    Core::Expected<void> AnnotationEngine::setPenParameters(const QColor &color, int lineWidth, int fontSize)
    {
        if (lineWidth <= 0)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("线宽必须是正数，收到的是 {}。请取 1 到 50 之间的整数"), lineWidth)});
        }
        if (fontSize <= 0)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("字号必须是正数，收到的是 {}。请取 8 到 72 之间的整数"), fontSize)});
        }

        m_color     = color;
        m_lineWidth = lineWidth;
        m_fontSize  = fontSize;
        for (const auto &tool: m_tools)
        {
            tool->setPenColor(color);
            tool->setLineWidth(lineWidth);
            tool->setFontSize(fontSize);
        }

        // 选中项跟着这次改动一起换样式：样式记在每条图形自己身上，工具上那三份只是后续图形的默认值，
        // 所以「选中着的那条」是用户改样式时唯一能合理指向的对象
        if (m_selection)
        {
            auto *tool = toolFor(m_selection->tool);
            if (tool != nullptr && m_selection->shapeIndex >= 0 && m_selection->shapeIndex < tool->shapeCount())
            {
                const AnnotationShape before = tool->shapeAt(m_selection->shapeIndex);
                // 三项全等就别进栈：点一下已经在用的那个颜色不该凭空造出一步「可撤销」
                if (before.color != color || before.lineWidth != lineWidth || before.fontSize != fontSize)
                {
                    // 快照取在改动之前：撤销栈里存的就是这条图形原来的三个样式值
                    m_undoStack.push(new RestyleShapeCommand(tool, m_selection->shapeIndex, before, color, lineWidth, fontSize));
                }
            }
        }
        return {};
    }

    void AnnotationEngine::beginStroke(const QPoint &point)
    {
        if (auto *tool = toolFor(m_activeTool); tool != nullptr)
        {
            tool->begin(point);
        }
    }

    void AnnotationEngine::moveStroke(const QPoint &point)
    {
        if (auto *tool = toolFor(m_activeTool); tool != nullptr)
        {
            tool->update(point);
        }
    }

    Core::Expected<AnnotationSelection> AnnotationEngine::endStroke()
    {
        auto *tool = toolFor(m_activeTool);
        if (tool == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("当前工具种类没有对应的工具实现，请检查 setActiveTool 与构造列表")});
        }

        const int  index     = tool->shapeCount();
        const auto committed = tool->end();
        if (!committed)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiText("这一笔没有落定：尺寸太小或内容为空。请按住鼠标拖出可见的范围后再松手").toStdString()});
        }

        m_undoStack.push(new InsertShapeCommand(tool, index, *committed));
        return AnnotationSelection{m_activeTool, index};
    }

    Core::Expected<void> AnnotationEngine::setPendingText(const QString &text)
    {
        auto *tool = toolFor(ToolKind::Text);
        if (tool == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("文字工具不在册")});
        }
        if (m_activeTool != ToolKind::Text)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiText("只有文字工具在用时才需要设定文本内容。请先切到文字工具再输入").toStdString()});
        }

        static_cast<TextTool *>(tool)->setPendingText(text);
        return {};
    }

    std::optional<AnnotationSelection> AnnotationEngine::selectionAt(const QPoint &point) const
    {
        // 倒序遍历：后登记的工具压在上面，用户点的就是看得见的那一条
        for (int index = static_cast<int>(m_tools.size()) - 1; index >= 0; --index)
        {
            const auto &tool = m_tools[index];
            const auto  hit  = tool->hitTest(point);
            if (hit >= 0)
            {
                return AnnotationSelection{tool->kind(), hit};
            }
        }
        return std::nullopt;
    }

    Core::Expected<void> AnnotationEngine::select(const AnnotationSelection &selection)
    {
        auto *tool = toolFor(selection.tool);
        if (tool == nullptr || selection.shapeIndex < 0 || selection.shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("要选中的图形不在册：工具 0x{:X} 索引 {}。请重新点选"),
                                                                            static_cast<int>(selection.tool), selection.shapeIndex)});
        }

        m_selection = selection;
        emit canvasChanged();
        return {};
    }

    const std::optional<AnnotationSelection> &AnnotationEngine::selection() const noexcept
    {
        return m_selection;
    }

    std::optional<QRect> AnnotationEngine::selectionBounds() const
    {
        if (!m_selection)
        {
            return {};
        }

        // 撤销/重做之后索引可能已经不在册，这里按「问不到就回空」处理：界面画不出选框是可接受的退化，
        // 而把越界索引当成错误报给用户就只是噪音
        const auto *tool = toolFor(m_selection->tool);
        if (tool == nullptr || m_selection->shapeIndex < 0 || m_selection->shapeIndex >= tool->shapeCount())
        {
            return {};
        }
        return tool->boundingBoxOf(tool->shapeAt(m_selection->shapeIndex));
    }

    void AnnotationEngine::paintShapes(QPainter &painter) const
    {
        if (m_canvas.image.isNull())
        {
            return;
        }

        // 与 exportImage 同一套倍比算法：屏显与导出必须取同样的源像素，否则马赛克会显示在一个位置、保存在另一个位置
        const qreal        pixelScale = m_canvas.logicalSize.width() > 0 ? static_cast<qreal>(m_canvas.image.width()) / m_canvas.logicalSize.width() : 1.0;
        const PaintContext context{m_canvas.image, pixelScale};
        for (const auto &tool: m_tools)
        {
            tool->paint(painter, context);
        }
    }

    Core::Expected<void> AnnotationEngine::moveSelectionBy(const QPoint &delta)
    {
        if (!m_selection)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，无法移动。请先点中要移动的那一条")});
        }

        auto *tool = toolFor(m_selection->tool);
        if (tool == nullptr || m_selection->shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了（多半刚被撤销或删除）。请重新点选")});
        }

        m_undoStack.push(new MoveShapeCommand(tool, m_selection->shapeIndex, delta));
        return {};
    }

    Core::Expected<void> AnnotationEngine::setTextOfSelection(const QString &text)
    {
        if (!m_selection)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，改不了文字。请先双击要改的那条文字")});
        }
        // 序号图形的文本是从编号派生的，让它被随手改掉就等于把编号与显示分家
        if (m_selection->tool != ToolKind::Text)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的不是文字图形，改不了内容。只有文字工具画出来的那几条可以双击重编")});
        }
        if (text.isEmpty())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("文字不能是空的：不要这一条就按 Delete 删掉，或按 Ctrl+Z 撤销这次改动")});
        }

        auto *tool = toolFor(m_selection->tool);
        if (tool == nullptr || m_selection->shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了（多半刚被撤销或删除）。请重新点选")});
        }

        m_undoStack.push(new SetTextCommand(tool, m_selection->shapeIndex, tool->shapeAt(m_selection->shapeIndex).text, text));
        return {};
    }

    std::optional<QString> AnnotationEngine::textOfSelection() const
    {
        if (!m_selection || m_selection->tool != ToolKind::Text)
        {
            return std::nullopt;
        }
        const auto *tool = toolFor(m_selection->tool);
        if (tool == nullptr || m_selection->shapeIndex >= tool->shapeCount())
        {
            return std::nullopt;
        }
        return tool->shapeAt(m_selection->shapeIndex).text;
    }

    Core::Expected<void> AnnotationEngine::removeSelection()
    {
        if (!m_selection)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，无需删除。请先点中要删的那一条")});
        }

        auto *tool = toolFor(m_selection->tool);
        if (tool == nullptr || m_selection->shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了。请重新点选")});
        }

        const AnnotationShape removed = tool->shapeAt(m_selection->shapeIndex);
        m_undoStack.push(new RemoveShapeCommand(tool, m_selection->shapeIndex, removed));
        m_selection.reset();
        return {};
    }

    bool AnnotationEngine::canUndo() const
    {
        return m_undoStack.canUndo();
    }

    bool AnnotationEngine::canRedo() const
    {
        return m_undoStack.canRedo();
    }

    Core::Expected<void> AnnotationEngine::undo()
    {
        if (!m_undoStack.canUndo())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有可撤销的标注动作了")});
        }
        m_undoStack.undo();
        // 撤销可能把选中的那条抹掉：索引失效就丢掉选中，别让后续操作打到别的图形上
        if (m_selection)
        {
            if (const auto resolved = toolFor(m_selection->tool); resolved == nullptr || m_selection->shapeIndex >= resolved->shapeCount())
            {
                m_selection.reset();
            }
        }
        return {};
    }

    Core::Expected<void> AnnotationEngine::redo()
    {
        if (!m_undoStack.canRedo())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有可重做的标注动作了")});
        }
        m_undoStack.redo();
        return {};
    }

    AnnotationTool *AnnotationEngine::toolFor(ToolKind kind) const noexcept
    {
        for (const auto &tool: m_tools)
        {
            if (tool->kind() == kind)
            {
                return tool.get();
            }
        }
        return nullptr;
    }

} // namespace GyanisScribe::Annotation
