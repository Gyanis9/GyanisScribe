#include "Base/Types.h"
#include "Core/Capture/IScreenCaptureSource.h"

#include <QImage>

#include <gtest/gtest.h>

#include <vector>

using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IScreenCaptureSource;

namespace
{
    CapturedImage makeFrame()
    {
        CapturedImage frame;
        frame.image = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
        frame.image.fill(Qt::red);
        frame.logicalSize = QSize(4, 4);
        return frame;
    }

    class DelegatingFakeSource final : public IScreenCaptureSource
    {
    public:
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &screenName) const override
        {
            requestedScreens.push_back(screenName);
            return makeFrame();
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(quintptr) const override
        {
            return makeFrame();
        }

        mutable std::vector<QString> requestedScreens;
    };
} // namespace

TEST(ScreenCaptureSource, TryGrabScreenDefaultsToGrabScreen)
{
    DelegatingFakeSource source;
    const auto           frame = source.tryGrabScreen(QStringLiteral("FakeScreen-1"));
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(source.requestedScreens, (std::vector<QString>{QStringLiteral("FakeScreen-1")}));
}
