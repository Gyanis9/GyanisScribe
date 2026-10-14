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
#include "Annotation/PolylineTool.h"
#include "Annotation/MagnifierTool.h"
#include "Annotation/RectangleTool.h"
#include "Annotation/ShapeTransform.h"
#include "Annotation/TextTool.h"
#include "Core/Text.h"

#include <QImage>
#include <QPainter>
#include <QUndoCommand>

#include <algorithm>
#include <format>

namespace GyanisScribe::Annotation
{
    namespace
    {
        /// 改样式命令的合并标识，取值只在本文件内用来分派：平移、几何与层叠各另有一个编号
        constexpr int kRestyleCommandTypeId = 1;

        /// 平移命令的合并标识：与样式各自一个编号，改线宽与挪位置这两趟手势不许互相并成一步
        constexpr int kMoveCommandTypeId = 2;

        /// 缩放手势的合并标识
        constexpr int kResizeCommandTypeId = 3;

        /// 旋转手势的合并标识：缩放与旋转各自一趟手势，不互相并步
        constexpr int kRotateCommandTypeId = 4;

        /// 粘贴时副本相对原件每轮的偏移量，逻辑像素：叠得死死的会让人以为没粘上
        constexpr int kPasteOffsetStride = 8;

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
         * @brief 平移一组图形（多选时整组一起走）
         * @details 撤销用等量反向位移：记下位移本身，而不是记下移动前后的两个位置。
         *          同一组图形的连续平移并成一步：拖一趟会发几十次位移，方向键逐格也能连按好几次，
         *          不并的话撤销一次只退一格，用户只能一直按着 Ctrl+Z 往回跑。
         */
        class MoveShapesCommand : public QUndoCommand
        {
        public:
            /// 一条被平移的图形：哪个工具的哪一条
            struct Target
            {
                AnnotationTool *tool  = nullptr; ///< 目标工具，所有权在引擎
                int             index = -1;      ///< 图形索引

                [[nodiscard]] bool operator==(const Target &) const = default;
            };

            MoveShapesCommand(std::vector<Target> targets, const QPoint &delta, QUndoCommand *parent = nullptr) :
                QUndoCommand(parent), m_targets(std::move(targets)), m_delta(delta)
            {
            }

            void redo() override
            {
                for (const auto &target: m_targets)
                {
                    static_cast<void>(target.tool->translateShapeBy(target.index, m_delta));
                }
            }

            void undo() override
            {
                for (const auto &target: m_targets)
                {
                    static_cast<void>(target.tool->translateShapeBy(target.index, -m_delta));
                }
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
                const auto &next = *static_cast<const MoveShapesCommand *>(other);
                // 只并同一组图形的位移：换了工具或换了序号，那就是另一趟意图
                if (next.m_targets != m_targets)
                {
                    return false;
                }

                // Qt 的 QUndoStack::push() 先跑新命令的 redo() 再试合并，位移已经落到图形上：
                // 这里只把两段累加成一个总位移，撤销取的是反向总量，回到这一趟开始之前
                m_delta += next.m_delta;
                return true;
            }

        private:
            std::vector<Target> m_targets; ///< 被平移的图形清单
            QPoint              m_delta;   ///< 位移量
        };

        /**
         * @brief 改一组图形的样式
         * @details 同一组图形的连续改动并成一步：线宽滑块拖一趟会发好几次值，撤销一次就该回到
         *          抬手之前，而不是让用户按好几下 Ctrl+Z。合并靠 id() 相同才会被栈尝试。
         */
        class RestyleShapesCommand : public QUndoCommand
        {
        public:
            /// 一条被改样式的图形与它改动前的那一份样式
            struct Target
            {
                AnnotationTool *tool  = nullptr; ///< 目标工具，所有权在引擎
                int             index = -1;      ///< 图形索引
                PenParameters   before;          ///< 改动前的整套样式，撤销回它
            };

            RestyleShapesCommand(std::vector<Target> targets, const PenParameters &after) : m_targets(std::move(targets)), m_after(after)
            {
            }

            void redo() override
            {
                for (const auto &target: m_targets)
                {
                    static_cast<void>(target.tool->restyleShapeAt(target.index, m_after));
                }
            }

            void undo() override
            {
                for (const auto &target: m_targets)
                {
                    static_cast<void>(target.tool->restyleShapeAt(target.index, target.before));
                }
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
                const auto &next = *static_cast<const RestyleShapesCommand *>(other);
                if (next.m_targets.size() != m_targets.size())
                {
                    return false;
                }
                for (std::size_t position = 0; position < m_targets.size(); ++position)
                {
                    if (next.m_targets[position].tool != m_targets[position].tool || next.m_targets[position].index != m_targets[position].index)
                    {
                        return false;
                    }
                }

                // m_before 保持第一次那一份：撤销要回到这一轮手势开始之前，而不是上一个中间档
                m_after = next.m_after;
                return true;
            }

        private:
            std::vector<Target> m_targets; ///< 被改样式的图形清单与各自的「之前」
            PenParameters       m_after;   ///< 目标样式
        };

        /**
         * @brief 整条替换一条图形（缩放与旋转走的这一条）
         * @details 命令只认「替换前」与「替换后」两份整条图形：缩放与旋转的中间量都由引擎按
         *          开场时那一份原样重算，所以合并只需要换掉「之后」那一份。
         *          合并标识由调用方给（缩放与旋转各一个），两趟手势不许互相并步。
         */
        class ReplaceShapeCommand : public QUndoCommand
        {
        public:
            ReplaceShapeCommand(const int typeId, AnnotationTool *tool, const int index, const AnnotationShape &before, const AnnotationShape &after) :
                m_typeId(typeId), m_tool(tool), m_index(index), m_before(before), m_after(after)
            {
            }

            void redo() override
            {
                static_cast<void>(m_tool->replaceShapeAt(m_index, m_after));
            }

            void undo() override
            {
                static_cast<void>(m_tool->replaceShapeAt(m_index, m_before));
            }

            [[nodiscard]] int id() const override
            {
                return m_typeId;
            }

            bool mergeWith(const QUndoCommand *other) override
            {
                if (other->id() != m_typeId)
                {
                    return false;
                }
                const auto &next = *static_cast<const ReplaceShapeCommand *>(other);
                if (next.m_tool != m_tool || next.m_index != m_index)
                {
                    return false;
                }

                // m_before 保持开场时那一份：撤销回到这一趟手势开始之前
                m_after = next.m_after;
                return true;
            }

        private:
            int             m_typeId; ///< 合并标识：缩放与旋转各一个
            AnnotationTool *m_tool;   ///< 目标工具，所有权在引擎
            int             m_index;  ///< 图形索引
            AnnotationShape m_before; ///< 改动之前的那一份图形
            AnnotationShape m_after;  ///< 目标图形
        };

        /**
         * @brief 改一批图形的全局层叠号
         * @details 换号就是换层：命令按「谁从几号换到几号」成对存下来，撤销逐个换回。
         *          一次调整（含多选整组）是一步撤销，不合并——每一次按键都是一次独立意图。
         */
        class ReorderShapesCommand : public QUndoCommand
        {
        public:
            /// 一条被换号的图形
            struct Entry
            {
                AnnotationTool *tool   = nullptr; ///< 目标工具，所有权在引擎
                int             index  = -1;      ///< 图形索引
                int             before = 0;       ///< 改动前的层叠号
                int             after  = 0;       ///< 目标层叠号
            };

            explicit ReorderShapesCommand(std::vector<Entry> entries, QUndoCommand *parent = nullptr) : QUndoCommand(parent), m_entries(std::move(entries))
            {
            }

            void redo() override
            {
                for (const auto &entry: m_entries)
                {
                    setZ(entry, entry.after);
                }
            }

            void undo() override
            {
                for (const auto &entry: m_entries)
                {
                    setZ(entry, entry.before);
                }
            }

        private:
            static void setZ(const Entry &entry, const int zOrder)
            {
                AnnotationShape shape = entry.tool->shapeAt(entry.index);
                shape.zOrder          = zOrder;
                static_cast<void>(entry.tool->replaceShapeAt(entry.index, shape));
            }

            std::vector<Entry> m_entries; ///< 被换号的图形与它们的前后层叠号
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
        // 折线登记在橡皮擦之前：擦除压在它上面，折线自己按落定顺序与别的笔画同层
        m_tools.insert(m_tools.end() - 1, std::make_unique<PolylineTool>());
        // 放大镜同样登记在橡皮擦之前：它的插图是展示件，与别的笔画同层
        m_tools.insert(m_tools.end() - 1, std::make_unique<MagnifierTool>());

        // 参数一次下发：切工具后笔还保持用户调过的那个粗细，才不会出现「换工具就得重调」
        applyPenParametersToTools();

        QObject::connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] { emit canvasChanged(); });
    }

    AnnotationEngine::~AnnotationEngine() = default;

    void AnnotationEngine::loadCanvas(Core::CapturedImage image)
    {
        m_canvas = std::move(image);

        // 换画布等于换一张纸：旧图形既不在新帧上，撤销回去也只会把用户引到错的地方
        m_undoStack.clear();
        m_selection.clear();
        m_handleDrag.reset();
        m_topZ        = 0;
        m_pasteSeries = 0;
        // 剪贴板不清：复制的是一条标注的副本，粘到下一张图上接着用正是它存在的意义
        for (const auto &tool: m_tools)
        {
            while (tool->shapeCount() > 0)
            {
                static_cast<void>(tool->takeShapeAt(0));
            }
        }
        emit canvasChanged();
    }

    void AnnotationEngine::refreshCanvas(Core::CapturedImage image)
    {
        // 只换底图这一份：图形、选中、层序、编号与撤销历史全部原地留着。
        // 取底图像素的工具（马赛克、模糊、放大镜）下一次绘制时自然读到新底图
        m_canvas = std::move(image);
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
        QPainter           painter(&result);
        // 图形记的是逻辑坐标，底图是物理像素：缩放画家让两处用同一套数字，导出与屏显才不会分家
        painter.scale(pixelScale, pixelScale);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // 按全局层叠号排序后逐条烘：后画的压在先画的上面，置顶/置底也在这里生效
        for (const auto &entry: zOrderedEntries())
        {
            entry.tool->paintShapeAt(entry.index, painter, bakeContext);
        }
        painter.end();

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

    Core::Expected<void> AnnotationEngine::setPenParameters(const PenParameters &parameters)
    {
        if (parameters.lineWidth <= 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("线宽必须是正数，收到的是 {}。请取 1 到 50 之间的整数"), parameters.lineWidth)});
        }
        if (parameters.fontSize <= 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("字号必须是正数，收到的是 {}。请取 8 到 72 之间的整数"), parameters.fontSize)});
        }

        m_penParameters = parameters;
        applyPenParametersToTools();

        // 选中的图形跟着这次改动一起换样式：样式记在每条图形自己身上，工具上那一份只是后续图形的默认值，
        // 所以「选中着的那几条」是用户改样式时唯一能合理指向的对象
        std::vector<RestyleShapesCommand::Target> targets;
        bool                                      anyChanged = false;
        for (const auto &item: m_selection)
        {
            auto *tool = toolFor(item.tool);
            if (tool == nullptr || item.shapeIndex < 0 || item.shapeIndex >= tool->shapeCount())
            {
                continue;
            }
            // 快照取在改动之前：撤销栈里存的就是这条图形原来的那一整套样式值
            const PenParameters before = penParametersOf(tool->shapeAt(item.shapeIndex));
            targets.push_back({tool, item.shapeIndex, before});
            anyChanged = anyChanged || before != parameters;
        }
        // 整组全等就别进栈：点一下已经在用的那个颜色不该凭空造出一步「可撤销」
        if (anyChanged)
        {
            m_undoStack.push(new RestyleShapesCommand(std::move(targets), parameters));
        }
        return {};
    }

    void AnnotationEngine::applyPenParametersToTools()
    {
        for (const auto &tool: m_tools)
        {
            tool->setPenColor(m_penParameters.color);
            tool->setLineWidth(m_penParameters.lineWidth);
            tool->setFontSize(m_penParameters.fontSize);
            tool->setFillStyle(m_penParameters.fillStyle);
            tool->setRoundedRect(m_penParameters.roundedRect);
            tool->setLineStyle(m_penParameters.lineStyle);
            tool->setDoubleHeaded(m_penParameters.doubleHeaded);
            tool->setArrowReversed(m_penParameters.arrowReversed);
            tool->setElliptical(m_penParameters.elliptical);
            tool->setMosaicBlockSize(m_penParameters.mosaicBlockSize);
            tool->setHighlightAlpha(m_penParameters.highlightAlpha);
            tool->setHighlightWidthFactor(m_penParameters.highlightWidthFactor);
            tool->setArrowHeadStyle(m_penParameters.arrowHeadStyle);
            tool->setArrowHeadScale(m_penParameters.arrowHeadScale);
            tool->setFontFamily(m_penParameters.fontFamily);
            tool->setTextBold(m_penParameters.textBold);
            tool->setTextItalic(m_penParameters.textItalic);
            tool->setTextOutline(m_penParameters.textOutline);
            tool->setTextBackground(m_penParameters.textBackground);
            tool->setTextStrokeColorStep(m_penParameters.textStrokeColorStep);
            tool->setTextOutlineWidthStep(m_penParameters.textOutlineWidthStep);
            tool->setTextPlateColorStep(m_penParameters.textPlateColorStep);
        }
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
            // 多步工具（折线）半途松手：在途图形还在手里，等下一个折点或双击收笔——
            // 这不是失败，错误码用 Busy 让界面与日志把它与「真没画成」分开
            if (tool->hasPending())
            {
                return std::unexpected(Core::Error{Core::ErrorCode::Busy, GyanisScribe::Core::uiTextUtf8("这一笔还没收：折线继续点下一个折点，双击收笔")});
            }
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiText("这一笔没有落定：尺寸太小或内容为空。请按住鼠标拖出可见的范围后再松手").toStdString()});
        }

        // 落定这一刻领号：新图形总压在最上层，「后画的压在先画的上面」就靠这个号
        AnnotationShape stamped = *committed;
        stamped.zOrder          = ++m_topZ;
        m_undoStack.push(new InsertShapeCommand(tool, index, stamped));
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

    Core::Expected<AnnotationSelection> AnnotationEngine::finishPendingStroke()
    {
        auto *tool = toolFor(m_activeTool);
        if (tool == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("当前工具种类没有对应的工具实现，请检查 setActiveTool 与构造列表")});
        }

        tool->requestFinish();
        const int  index     = tool->shapeCount();
        const auto committed = tool->end();
        if (!committed)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiText("现在没有在途的笔画可以收笔。折线至少要点出两个折点").toStdString()});
        }

        // 与 endStroke 同一口径：落定即领号，压在现有全部图形之上
        AnnotationShape stamped = *committed;
        stamped.zOrder          = ++m_topZ;
        m_undoStack.push(new InsertShapeCommand(tool, index, stamped));
        return AnnotationSelection{m_activeTool, index};
    }

    Core::Expected<void> AnnotationEngine::setNextNumber(const int number)
    {
        auto *tool = toolFor(ToolKind::Number);
        if (tool == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("序号工具不在册")});
        }
        static_cast<NumberTool *>(tool)->setNextNumber(number);
        return {};
    }

    void AnnotationEngine::resetNumbering()
    {
        if (auto *tool = toolFor(ToolKind::Number); tool != nullptr)
        {
            static_cast<NumberTool *>(tool)->clearNextNumber();
        }
    }

    int AnnotationEngine::nextNumber() const
    {
        const auto *tool = toolFor(ToolKind::Number);
        // 工具恒在册（构造列表里就有它）；真缺了交回 1，让界面显示一个不含蓄的起点而不是崩溃
        return tool != nullptr ? static_cast<const NumberTool *>(tool)->nextNumber() : 1;
    }

    Core::Expected<void> AnnotationEngine::setNumberGroup(const int group)
    {
        auto *tool = toolFor(ToolKind::Number);
        if (tool == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("序号工具不在册")});
        }
        static_cast<NumberTool *>(tool)->setNumberGroup(group);
        return {};
    }

    int AnnotationEngine::numberGroup() const
    {
        const auto *tool = toolFor(ToolKind::Number);
        return tool != nullptr ? static_cast<const NumberTool *>(tool)->numberGroup() : 1;
    }

    std::optional<AnnotationSelection> AnnotationEngine::selectionAt(const QPoint &point) const
    {
        // 按层叠号的倒序问：最上层的那条先答，用户点的就是看得见的那一条
        const auto entries = zOrderedEntries();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            if (it->tool->hitTestShapeAt(it->index, point))
            {
                return AnnotationSelection{it->tool->kind(), it->index};
            }
        }
        return std::nullopt;
    }

    bool AnnotationEngine::selectionIsValid(const AnnotationSelection &selection) const
    {
        const auto *tool = toolFor(selection.tool);
        return tool != nullptr && selection.shapeIndex >= 0 && selection.shapeIndex < tool->shapeCount();
    }

    Core::Expected<void> AnnotationEngine::select(const AnnotationSelection &selection)
    {
        if (!selectionIsValid(selection))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("要选中的图形不在册：工具 0x{:X} 索引 {}。请重新点选"),
                                                                            static_cast<int>(selection.tool), selection.shapeIndex)});
        }

        m_selection.assign(1, selection);
        emit canvasChanged();
        return {};
    }

    Core::Expected<void> AnnotationEngine::addToSelection(const AnnotationSelection &selection)
    {
        if (!selectionIsValid(selection))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("要加选的图形不在册：工具 0x{:X} 索引 {}。请重新点选"),
                                                                            static_cast<int>(selection.tool), selection.shapeIndex)});
        }

        // 已经在选中集里就什么都不做：Shift 连点同一条不该造出重复项，之后整组移动会把它挪两倍
        const bool alreadySelected = std::any_of(m_selection.begin(), m_selection.end(),
                                                 [&selection](const AnnotationSelection &item) { return item.tool == selection.tool && item.shapeIndex == selection.shapeIndex; });
        if (!alreadySelected)
        {
            m_selection.push_back(selection);
            emit canvasChanged();
        }
        return {};
    }

    Core::Expected<void> AnnotationEngine::toggleSelection(const AnnotationSelection &selection)
    {
        if (!selectionIsValid(selection))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("要反选的图形不在册：工具 0x{:X} 索引 {}。请重新点选"),
                                                                            static_cast<int>(selection.tool), selection.shapeIndex)});
        }

        const auto found = std::find_if(m_selection.begin(), m_selection.end(),
                                        [&selection](const AnnotationSelection &item) { return item.tool == selection.tool && item.shapeIndex == selection.shapeIndex; });
        if (found != m_selection.end())
        {
            m_selection.erase(found);
        } else
        {
            m_selection.push_back(selection);
        }
        emit canvasChanged();
        return {};
    }

    void AnnotationEngine::clearSelection()
    {
        if (m_selection.empty())
        {
            return;
        }
        m_selection.clear();
        emit canvasChanged();
    }

    const std::vector<AnnotationSelection> &AnnotationEngine::selections() const noexcept
    {
        return m_selection;
    }

    std::optional<QRect> AnnotationEngine::boundsOf(const AnnotationSelection &selection) const
    {
        // 撤销/重做之后索引可能已经不在册，这里按「问不到就回空」处理：界面画不出选框是可接受的退化，
        // 而把越界索引当成错误报给用户就只是噪音
        const auto *tool = toolFor(selection.tool);
        if (tool == nullptr || selection.shapeIndex < 0 || selection.shapeIndex >= tool->shapeCount())
        {
            return {};
        }
        return tool->boundingBoxOf(tool->shapeAt(selection.shapeIndex));
    }

    std::optional<QRect> AnnotationEngine::selectionBounds() const
    {
        std::optional<QRect> united;
        for (const auto &item: m_selection)
        {
            const auto box = boundsOf(item);
            if (!box)
            {
                continue;
            }
            united = united ? united->united(*box) : box;
        }
        return united;
    }

    std::optional<std::array<QPointF, 8>> AnnotationEngine::selectionFrame() const
    {
        // 多选不给手柄：八个把手该按哪一条的框摆没有共识，界面对多选只画联合虚线框
        if (m_selection.size() != 1)
        {
            return std::nullopt;
        }

        const auto *tool = toolFor(m_selection.front().tool);
        if (tool == nullptr || m_selection.front().shapeIndex < 0 || m_selection.front().shapeIndex >= tool->shapeCount())
        {
            return std::nullopt;
        }

        const auto corners  = tool->frameCornersOf(tool->shapeAt(m_selection.front().shapeIndex));
        const auto midpoint = [](const QPointF &a, const QPointF &b) { return (a + b) / 2.0; };
        return std::array<QPointF, 8>{corners[0], midpoint(corners[0], corners[1]), corners[1], midpoint(corners[1], corners[2]),
                                      corners[2], midpoint(corners[2], corners[3]), corners[3], midpoint(corners[3], corners[0])};
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
        for (const auto &entry: zOrderedEntries())
        {
            entry.tool->paintShapeAt(entry.index, painter, context);
        }
        // 在途图形压在最上层：它是「正在画的那一条」，缩在别的图形下面会看不见自己画的进度
        if (auto *active = toolFor(m_activeTool); active != nullptr)
        {
            active->paintPending(painter, context);
        }
    }

    std::vector<AnnotationEngine::ZEntry> AnnotationEngine::zOrderedEntries() const
    {
        std::vector<ZEntry> entries;
        for (const auto &tool: m_tools)
        {
            for (int index = 0; index < tool->shapeCount(); ++index)
            {
                entries.push_back({tool.get(), index, tool->shapeAt(index).zOrder});
            }
        }
        // 稳定排序：层叠号本该互不相同；真撞上了（老数据、手工构造）也保持工具登记顺序，不随机打乱
        std::stable_sort(entries.begin(), entries.end(), [](const ZEntry &left, const ZEntry &right) { return left.zOrder < right.zOrder; });
        return entries;
    }

    Core::Expected<void> AnnotationEngine::moveSelectionBy(const QPoint &delta)
    {
        if (m_selection.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，无法移动。请先点中要移动的那一条")});
        }

        std::vector<MoveShapesCommand::Target> targets;
        targets.reserve(m_selection.size());
        for (const auto &item: m_selection)
        {
            auto *tool = toolFor(item.tool);
            if (tool == nullptr || item.shapeIndex >= tool->shapeCount())
            {
                return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了（多半刚被撤销或删除）。请重新点选")});
            }
            targets.push_back({tool, item.shapeIndex});
        }

        m_undoStack.push(new MoveShapesCommand(std::move(targets), delta));
        return {};
    }

    Core::Expected<void> AnnotationEngine::setTextOfSelection(const QString &text)
    {
        if (m_selection.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，改不了文字。请先双击要改的那条文字")});
        }
        // 序号图形的文本是从编号派生的，让它被随手改掉就等于把编号与显示分家；
        // 多选里有整有零也说不清改的是哪一条，所以这里只认「恰好选中一条文字图形」
        if (m_selection.size() != 1 || m_selection.front().tool != ToolKind::Text)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiTextUtf8("改内容要恰好选中一条文字图形：多选或别的图形都改不了。只有文字工具画出来的那几条可以双击重编")});
        }
        if (text.isEmpty())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("文字不能是空的：不要这一条就按 Delete 删掉，或按 Ctrl+Z 撤销这次改动")});
        }

        const auto &subject = m_selection.front();
        auto       *tool    = toolFor(subject.tool);
        if (tool == nullptr || subject.shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了（多半刚被撤销或删除）。请重新点选")});
        }

        m_undoStack.push(new SetTextCommand(tool, subject.shapeIndex, tool->shapeAt(subject.shapeIndex).text, text));
        return {};
    }

    std::optional<QString> AnnotationEngine::textOfSelection() const
    {
        if (m_selection.size() != 1 || m_selection.front().tool != ToolKind::Text)
        {
            return std::nullopt;
        }
        const auto *tool = toolFor(m_selection.front().tool);
        if (tool == nullptr || m_selection.front().shapeIndex >= tool->shapeCount())
        {
            return std::nullopt;
        }
        return tool->shapeAt(m_selection.front().shapeIndex).text;
    }

    Core::Expected<void> AnnotationEngine::removeSelection()
    {
        if (m_selection.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，无需删除。请先点中要删的那一条")});
        }

        // 先收集再删：同一工具内删掉小索引会让大索引整体前移，按索引从大到小的次序删才不会互相踩
        struct Victim
        {
            AnnotationTool *tool;
            int             index;
            AnnotationShape shape;
        };
        std::vector<Victim> victims;
        victims.reserve(m_selection.size());
        for (const auto &item: m_selection)
        {
            auto *tool = toolFor(item.tool);
            if (tool == nullptr || item.shapeIndex < 0 || item.shapeIndex >= tool->shapeCount())
            {
                return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了。请重新点选")});
            }
            victims.push_back({tool, item.shapeIndex, tool->shapeAt(item.shapeIndex)});
        }
        std::sort(victims.begin(), victims.end(), [](const Victim &left, const Victim &right) { return left.index > right.index; });

        if (victims.size() == 1)
        {
            m_undoStack.push(new RemoveShapeCommand(victims.front().tool, victims.front().index, victims.front().shape));
        } else
        {
            // 整组删除是一步：撤销一次把这一批都放回来。宏名只落在 QUndoStack 里，界面上不显示
            m_undoStack.beginMacro(QStringLiteral("删除选中标注"));
            for (const auto &victim: victims)
            {
                m_undoStack.push(new RemoveShapeCommand(victim.tool, victim.index, victim.shape));
            }
            m_undoStack.endMacro();
        }
        m_selection.clear();
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
        m_handleDrag.reset();
        // 撤销可能把选中的那条抹掉：索引失效的就从选中集里摘掉，别让后续操作打到别的图形上
        std::erase_if(m_selection, [this](const AnnotationSelection &item) { return !selectionIsValid(item); });
        return {};
    }

    Core::Expected<void> AnnotationEngine::redo()
    {
        if (!m_undoStack.canRedo())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有可重做的标注动作了")});
        }
        m_undoStack.redo();
        m_handleDrag.reset();
        return {};
    }

    Core::Expected<void> AnnotationEngine::beginSelectionDrag(const SelectionHandle handle, const QPoint &point)
    {
        if (handle == SelectionHandle::None)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有抓住任何手柄。请对准选中框上的小方块或框外的圆点再拖")});
        }
        if (m_selection.size() != 1)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("缩放手柄只在选中一条图形时出现。请先只选中一条再拖手柄")});
        }

        const auto &subject = m_selection.front();
        auto       *tool    = toolFor(subject.tool);
        if (tool == nullptr || subject.shapeIndex < 0 || subject.shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了（多半刚被撤销或删除）。请重新点选")});
        }

        HandleDrag drag;
        drag.handle     = handle;
        drag.tool       = subject.tool;
        drag.shapeIndex = subject.shapeIndex;
        drag.before     = tool->shapeAt(subject.shapeIndex);
        drag.localBox   = QRectF{tool->localBoundingBoxOf(drag.before)};
        // 旋转的起点角取按下点相对中心的方向：之后每一点都跟这个方向比，抬手时图形正好转了多少一眼可算
        drag.startAngle = angleDegreesOf(QPointF{point}, drag.localBox.center());
        m_handleDrag    = drag;
        return {};
    }

    Core::Expected<void> AnnotationEngine::updateSelectionDrag(const QPoint &point, const SelectionDragModifiers &modifiers)
    {
        if (!m_handleDrag)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("现在没有在拖手柄。请先按下选中框上的手柄再拖")});
        }

        const HandleDrag session = *m_handleDrag;
        auto            *tool    = toolFor(session.tool);
        if (tool == nullptr || session.shapeIndex < 0 || session.shapeIndex >= tool->shapeCount())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("拖的那条图形已经不在了。请重新点选")});
        }

        AnnotationShape target = session.before;
        int             typeId = kResizeCommandTypeId;
        if (session.handle == SelectionHandle::Rotate)
        {
            typeId = kRotateCommandTypeId;
            // 归一化角度差：拖着绕过对侧时那一步跨过 ±180°，不归一图形会跟着猛转一整圈
            const qreal current = angleDegreesOf(QPointF{point}, session.localBox.center());
            const qreal delta   = normalizedAngleDelta(current - session.startAngle);
            target.rotationDeg  = normalizedAngleDelta(session.before.rotationDeg + delta);
        } else
        {
            // 缩放从开场那一份外接框重算：事件点先按图形的旋转角映回局部坐标，
            // 于是旋转过的图形也是沿它自己的两条边伸缩，而不是沿屏幕的横竖；
            // Ctrl/Shift 两档由修饰键一并带进几何
            const QPointF local  = unrotatedPoint(QPointF{point}, session.localBox.center(), session.before.rotationDeg);
            const QRectF  newBox = resizedBox(session.localBox, session.handle, local, kMinimumResizedBoxSide, modifiers.fromCenter, modifiers.proportional);
            target               = shapeInBox(session.before, session.localBox, newBox);
            target.rotationDeg   = session.before.rotationDeg;
        }

        // 每一点都从 session.before 重算并覆盖命令的终点，中途的取整不会一格格攒成漂移；
        // 同一条图形的连续拖动并成一步撤销
        m_undoStack.push(new ReplaceShapeCommand(typeId, tool, session.shapeIndex, session.before, target));
        return {};
    }

    void AnnotationEngine::endSelectionDrag()
    {
        m_handleDrag.reset();
    }

    Core::Expected<void> AnnotationEngine::raiseSelection()
    {
        return reorderSelection(ReorderMode::Raise);
    }

    Core::Expected<void> AnnotationEngine::lowerSelection()
    {
        return reorderSelection(ReorderMode::Lower);
    }

    Core::Expected<void> AnnotationEngine::bringSelectionToFront()
    {
        return reorderSelection(ReorderMode::ToFront);
    }

    Core::Expected<void> AnnotationEngine::sendSelectionToBack()
    {
        return reorderSelection(ReorderMode::ToBack);
    }

    Core::Expected<void> AnnotationEngine::reorderSelection(const ReorderMode mode)
    {
        if (m_selection.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，改不了层叠顺序。请先点中要挪层的那一条")});
        }

        // 层叠表按升序拿（最底层的在前）；选中的那几条做个标记，跟着排
        auto              entries = zOrderedEntries();
        std::vector<bool> selected;
        selected.reserve(entries.size());
        int selectedCount = 0;
        for (const auto &entry: entries)
        {
            const bool hit = std::any_of(m_selection.begin(), m_selection.end(),
                                         [&entry](const AnnotationSelection &item) { return item.tool == entry.tool->kind() && item.shapeIndex == entry.index; });
            selected.push_back(hit);
            selectedCount += hit ? 1 : 0;
        }
        if (selectedCount == 0)
        {
            // 选中的那几条都已经被撤没了：与「没选中」同一句交代，不写进栈
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了（多半刚被撤销或删除）。请重新点选")});
        }

        std::vector<ZEntry> arranged     = entries;
        std::vector<bool>   arrangedMask = selected;
        switch (mode)
        {
            case ReorderMode::Raise:
                // 从最上面那条开始逐个上浮：头顶挨着的是没选中的就换位，选中的连成一块就整块上浮
                for (int position = static_cast<int>(arranged.size()) - 1; position >= 0; --position)
                {
                    if (arrangedMask[position] && position + 1 < static_cast<int>(arranged.size()) && !arrangedMask[position + 1])
                    {
                        std::swap(arranged[position], arranged[position + 1]);
                        // vector<bool> 的元素是代理引用，进不了 std::swap，手动换
                        const bool aboveSelected   = arrangedMask[position + 1];
                        arrangedMask[position + 1] = arrangedMask[position];
                        arrangedMask[position]     = aboveSelected;
                    }
                }
                break;
            case ReorderMode::Lower:
                // 镜像：从最底下那条开始逐个下沉
                for (int position = 0; position < static_cast<int>(arranged.size()); ++position)
                {
                    if (arrangedMask[position] && position > 0 && !arrangedMask[position - 1])
                    {
                        std::swap(arranged[position], arranged[position - 1]);
                        const bool belowSelected   = arrangedMask[position - 1];
                        arrangedMask[position - 1] = arrangedMask[position];
                        arrangedMask[position]     = belowSelected;
                    }
                }
                break;
            case ReorderMode::ToFront:
            {
                std::vector<ZEntry> reordered;
                for (std::size_t position = 0; position < arranged.size(); ++position)
                {
                    if (!arrangedMask[position])
                    {
                        reordered.push_back(arranged[position]);
                    }
                }
                for (std::size_t position = 0; position < arranged.size(); ++position)
                {
                    if (arrangedMask[position])
                    {
                        reordered.push_back(arranged[position]);
                    }
                }
                arranged = std::move(reordered);
                break;
            }
            case ReorderMode::ToBack:
            {
                std::vector<ZEntry> reordered;
                for (std::size_t position = 0; position < arranged.size(); ++position)
                {
                    if (arrangedMask[position])
                    {
                        reordered.push_back(arranged[position]);
                    }
                }
                for (std::size_t position = 0; position < arranged.size(); ++position)
                {
                    if (!arrangedMask[position])
                    {
                        reordered.push_back(arranged[position]);
                    }
                }
                arranged = std::move(reordered);
                break;
            }
        }

        // 次序没动就别进栈：已经在顶上的再按一次「上移」是空动作，不该造出一步「可撤销」
        bool moved = false;
        for (std::size_t position = 0; position < arranged.size(); ++position)
        {
            if (arranged[position].tool != entries[position].tool || arranged[position].index != entries[position].index)
            {
                moved = true;
                break;
            }
        }
        if (!moved)
        {
            return {};
        }

        // 整摞重编成 0..n-1 的连续号：每次调整后层叠号都不留空档，下次调整的差值判据才有单一含义
        std::vector<ReorderShapesCommand::Entry> commandEntries;
        commandEntries.reserve(arranged.size());
        for (std::size_t position = 0; position < arranged.size(); ++position)
        {
            commandEntries.push_back({arranged[position].tool, arranged[position].index, arranged[position].zOrder, static_cast<int>(position)});
        }
        m_undoStack.push(new ReorderShapesCommand(std::move(commandEntries)));
        return {};
    }

    Core::Expected<void> AnnotationEngine::copySelection()
    {
        if (m_selection.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("没有选中任何图形，没有可复制的。请先点中要复制的那一条")});
        }

        // 按层叠号的升序抄：粘出来那批的相对压叠与原件一致，不是随手按点选顺序摞
        std::vector<std::pair<ToolKind, AnnotationShape>> copies;
        for (const auto &entry: zOrderedEntries())
        {
            const bool hit = std::any_of(m_selection.begin(), m_selection.end(),
                                         [&entry](const AnnotationSelection &item) { return item.tool == entry.tool->kind() && item.shapeIndex == entry.index; });
            if (hit)
            {
                copies.emplace_back(entry.tool->kind(), entry.tool->shapeAt(entry.index));
            }
        }
        if (copies.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("选中的图形已经不在了。请重新点选")});
        }

        m_clipboard   = std::move(copies);
        m_pasteSeries = 0;
        return {};
    }

    Core::Expected<void> AnnotationEngine::pasteClipboard()
    {
        if (m_clipboard.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("剪贴板里还没有图形：先用 Ctrl+C 复制一条，再按 Ctrl+V 粘贴")});
        }

        // 连续粘贴逐次多偏一格：两次都落在同一点会让人以为只粘上了一份
        ++m_pasteSeries;
        const QPoint offset{kPasteOffsetStride * m_pasteSeries, kPasteOffsetStride * m_pasteSeries};

        std::vector<AnnotationSelection> pasted;
        m_undoStack.beginMacro(QStringLiteral("粘贴标注"));
        for (const auto &[kind, shape]: m_clipboard)
        {
            auto *tool = toolFor(kind);
            if (tool == nullptr)
            {
                continue;
            }
            AnnotationShape copy = shape;
            translateShape(copy, offset);
            copy.zOrder      = ++m_topZ; // 粘出来的压在最上层
            const int placed = tool->shapeCount();
            m_undoStack.push(new InsertShapeCommand(tool, placed, copy));
            pasted.push_back({kind, placed});
        }
        m_undoStack.endMacro();

        if (pasted.empty())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("剪贴板里的工具都不在册，粘贴没有落点")});
        }
        // 粘出来的这批成为新的选中集：接着就能拖它、改它
        m_selection = std::move(pasted);
        emit canvasChanged();
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
