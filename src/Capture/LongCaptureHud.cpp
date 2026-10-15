#include "Capture/LongCaptureHud.h"

#include "Base/Text.h"
#include "Gui/Theme/DesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
    using GyanisScribe::Capture::LongCaptureHud;

    /// 控制条与选区之间留的空隙，逻辑像素：贴着边摆会把边框一起拍进下一帧
    constexpr int kHudRegionGap = 8;
} // namespace

namespace GyanisScribe::Capture
{
    QRect LongCaptureHud::positionFor(const QRect &region, const QRect &available, const QSize &hudSize)
    {
        if (hudSize.isEmpty())
        {
            return {};
        }

        // 四个方向按「下、上、右、左」试：往下摆最符合「接着往下滚」的阅读顺序
        const auto candidates = {QPoint{region.left(), region.bottom() + kHudRegionGap}, QPoint{region.left(), region.top() - kHudRegionGap - hudSize.height()},
                                 QPoint{region.right() + kHudRegionGap, region.top()}, QPoint{region.left() - kHudRegionGap - hudSize.width(), region.top()}};
        // 四个落点都是「贴着选区外侧再让开一格」算出来的，天然压不住选区；这里只挑第一个还在屏内的
        for (const auto &corner: candidates)
        {
            const QRect placed{corner, hudSize};
            if (available.contains(placed))
            {
                return placed;
            }
        }

        // 四个方向都放不下（选区几乎占满整屏）：贴到可用区右下角，至少不会被选区拍进去
        const QRect fallback{QPoint{available.right() - hudSize.width() + 1, available.bottom() - hudSize.height() + 1}, hudSize};
        return available.contains(fallback) ? fallback : QRect{available.topLeft(), hudSize};
    }

    LongCaptureHud::LongCaptureHud(QWidget *parent) : QWidget(parent)
    {
        // 工具窗 + 不吃焦点：滚动的是别的窗口，控制条抢了焦点就滚不动了
        setWindowTitle(GyanisScribe::Base::uiText("长截图控制条"));
        setAccessibleName(GyanisScribe::Base::uiText("长截图控制条"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setObjectName(QStringLiteral("hudRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(GyanisScribe::Gui::kSpacingSmall, GyanisScribe::Gui::kSpacingTiny, GyanisScribe::Gui::kSpacingSmall, GyanisScribe::Gui::kSpacingTiny);
        root->setSpacing(GyanisScribe::Gui::kSpacingTiny);

        auto *hint = new QLabel{GyanisScribe::Base::uiText("滚动要截的页面，接好后按完成"), this};
        hint->setAccessibleName(GyanisScribe::Base::uiText("长截图提示"));
        GyanisScribe::Gui::applyTextRole(*hint, GyanisScribe::Gui::TextRole::Body);
        root->addWidget(hint);

        m_readout = new QLabel{GyanisScribe::Base::uiText("还没接上任何一段"), this};
        m_readout->setAccessibleName(GyanisScribe::Base::uiText("已接高度"));
        // 等宽数字：每接一帧就跳一下，比例字体会让整条控制条左右抖
        GyanisScribe::Gui::applyTextRole(*m_readout, GyanisScribe::Gui::TextRole::Metric);
        root->addWidget(m_readout);

        auto *line = new QHBoxLayout;
        line->setSpacing(GyanisScribe::Gui::kSpacingSmall);
        root->addLayout(line);

        auto *cancelButton = new QPushButton{GyanisScribe::Base::uiText("取消"), this};
        cancelButton->setToolTip(GyanisScribe::Base::uiText("扔掉这一段，不产出长图"));
        cancelButton->setAccessibleName(GyanisScribe::Base::uiText("取消长截图"));
        auto *finishButton = new QPushButton{GyanisScribe::Base::uiText("完成"), this};
        finishButton->setToolTip(GyanisScribe::Base::uiText("结束拼接，把这张长图交给下一步"));
        finishButton->setAccessibleName(GyanisScribe::Base::uiText("完成长截图"));

        line->addWidget(cancelButton);
        line->addWidget(finishButton);

        connect(cancelButton, &QPushButton::clicked, this, &LongCaptureHud::cancelled);
        connect(finishButton, &QPushButton::clicked, this, &LongCaptureHud::finished);
    }

    LongCaptureHud::~LongCaptureHud() = default;

    void LongCaptureHud::setReadout(const QString &readout)
    {
        m_readout->setText(readout);
    }

    QSize LongCaptureHud::preferredHudSize() const
    {
        // 控件没上屏时 sizeHint 量不出来（布局要一次真正的 polish 才算得出），
        // 而摆位恰恰要在 show 之前定下来——给一份按内容算的下限，比退化成 0x0 有用
        const auto hinted = sizeHint();
        return hinted.isValid() && !hinted.isEmpty() ? hinted : QSize{260, 84};
    }

} // namespace GyanisScribe::Capture
