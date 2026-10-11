// 剪贴板取图的判据：只碰 QMimeData 与 QImage，两样都不需要 QApplication 实例，
// 因此这一组跟 TestShell 里别的用例共用同一个可执行体，不占「进程里唯一的那个应用对象」。

#include "App/ClipboardPin.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QMimeData>
#include <QSize>
#include <QString>
#include <QVariant>

#include <string>

using GyanisScribe::App::pinnableImageFrom;
using GyanisScribe::Core::ErrorCode;

namespace
{
    QImage makeImage()
    {
        QImage image{QSize{40, 20}, QImage::Format_ARGB32};
        image.fill(QColor{10, 120, 200});
        return image;
    }
} // namespace

/**
 * @brief 钉住剪贴板里有图时取出来的就是原样那张
 */
TEST(ClipboardPin, TakesTheImageAsIs)
{
    QMimeData data;
    data.setImageData(makeImage());

    const auto image = pinnableImageFrom(&data);
    ASSERT_TRUE(image.has_value()) << image.error().message;
    EXPECT_EQ(image->size(), QSize(40, 20)) << "取出来的不是剪贴板里那一张";
}

/**
 * @brief 钉住三条拒因：没有载荷、载荷里只有文本、挂了图像名目却没给像素
 * @details 三条都要带「怎么办」——这一条的入口在托盘上，附近没有输入框能把话说清楚；
 *          文本那一路还没做转贴图，拿它冒充图像会让用户贴出一张空图。
 */
TEST(ClipboardPin, RejectsWithoutAnImageAndSaysWhatToDo)
{
    // 没人拷贝过时，有的平台直接给一个空指针
    const auto nothing = pinnableImageFrom(nullptr);
    ASSERT_FALSE(nothing.has_value());
    EXPECT_EQ(nothing.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(nothing.error().message.find("没有图像"), std::string::npos);
    EXPECT_NE(nothing.error().message.find("截图热键"), std::string::npos) << "要给出替代做法，而不是只报一句没有";

    QMimeData textOnly;
    textOnly.setText(QStringLiteral("一段文字"));
    EXPECT_FALSE(pinnableImageFrom(&textOnly).has_value()) << "文本那一路还没有转贴图的做法，不许当成图像";

    // 挂了图像格式、内容却是空的：受保护内容与跨会话拷贝都长这样
    QMimeData blind;
    blind.setImageData(QVariant{});
    const auto empty = pinnableImageFrom(&blind);
    ASSERT_FALSE(empty.has_value()) << "空壳图像被当成一张能贴的图";
    EXPECT_NE(empty.error().message.find("读不出来"), std::string::npos) << "要说清是读不出，而不是没有";
}
