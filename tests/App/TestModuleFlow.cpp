// 跨模块流程的集成入口（需求 §10：覆盖截图、贴图、OCR 流程）。
// 单模块用例各测各的，这一条钉的是它们接在一起时坐标系还对不对：抓帧那一步刻意按生产形状
// 造——QImage 的倍比标成 1.0，真实缩放只住在 CapturedImage.devicePixelRatio 与 logicalSize 里
// （见 src/Capture/QScreenCaptureSource.cpp），于是「标注吃逻辑坐标 → 导出物理像素 → 贴图开窗」
// 这三步之间的换算一旦被漏掉，就只有这条链上看得见。

#include "Annotation/AnnotationEngine.h"
#include "Annotation/AnnotationTool.h"
#include "Annotation/PenParameters.h"
#include "Capture/CaptureService.h"
#include "Capture/ICaptureService.h"
#include "Capture/IScreenCaptureSource.h"
#include "Core/Paths.h"
#include "App/ClipboardPin.h"
#include "Core/Types.h"
#include "Ocr/IOcrEngine.h"
#include "Ocr/OcrEngineFactory.h"
#include "Pin/IPinWindow.h"
#include "Pin/PinManager.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsStore.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>

// 整份用例住在工程命名空间里：匿名命名空间在工程之外时 `Core::Expected` 这类写法认不到名字
// （实测 C2061/C2653），而 nest 进 GyanisScribe 就不用给每个类型加前缀
namespace GyanisScribe
{
    namespace
    {
        int                           argumentCount = 1;
        std::unique_ptr<QApplication> application;

        /// argv 缓冲要有静态存储期：QApplication 只存指针不拷贝
        inline static char argumentZero[] = "TestModuleFlow";
        char              *arguments[]    = {argumentZero, nullptr};

        constexpr int   kPhysicalWidth    = 900;
        constexpr int   kPhysicalHeight   = 600;
        constexpr int   kLogicalWidth     = 600;
        constexpr int   kLogicalHeight    = 400;
        constexpr qreal kDevicePixelRatio = 1.5;
        constexpr char  kFakeScreenName[] = "FakeScreen";

        /// 标注用的颜色：假屏只有白底黑字，这个色一出现就说明图形真的烘进导出图了
        const QColor kAnnotationColor(220, 30, 30);

        /// 画的那条矩形在逻辑坐标下的位置，与下面的物理换算是一对
        constexpr QRect kStrokeLogicalRect(50, 50, 200, 100);

        /**
         * @brief 把逻辑矩形换算成物理矩形
         * @param logical 界面坐标下的矩形
         * @return 像素缓冲里的矩形（向下取整左上、向上取整右下，与 ScreenUtils 同一套口径）
         */
        QRect toPhysical(const QRect &logical)
        {
            const auto left   = static_cast<int>(std::floor(logical.x() * kDevicePixelRatio));
            const auto top    = static_cast<int>(std::floor(logical.y() * kDevicePixelRatio));
            const auto right  = static_cast<int>(std::ceil((logical.x() + logical.width()) * kDevicePixelRatio));
            const auto bottom = static_cast<int>(std::ceil((logical.y() + logical.height()) * kDevicePixelRatio));
            return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
        }

        /**
         * @brief 造一帧假屏，形状与生产抓帧一致
         * @return 抓帧产物：QImage 是物理像素且倍比标成 1.0，缩放只写在 devicePixelRatio 与 logicalSize 里
         */
        Core::CapturedImage makeFakeFrame()
        {
            QImage image(kPhysicalWidth, kPhysicalHeight, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            image.setDevicePixelRatio(1.0);

            QPainter painter(&image);
            painter.setPen(Qt::black);
            QFont font = painter.font();
            font.setPointSize(64);
            painter.setFont(font);
            // 底图按物理像素摆：这一帧的倍比就是 1.0，与 QScreenCaptureSource 给回的形状同解
            painter.drawText(QPoint(30, 110), QStringLiteral("12345"));

            Core::CapturedImage frame;
            frame.image            = image;
            frame.logicalSize      = QSize(kLogicalWidth, kLogicalHeight);
            frame.devicePixelRatio = kDevicePixelRatio;
            frame.screenName       = QString::fromLatin1(kFakeScreenName);
            return frame;
        }

        /**
         * @brief 数一块矩形里有多少个像素正好是这个颜色
         * @param image 待数的图（物理像素）
         * @param region 要数的矩形，超出图外的部分自动裁掉
         * @return 命中像素数
         */
        int countPixelsOf(const QImage &image, const QRect &region)
        {
            const auto bounded = region.intersected(image.rect());
            int        hits    = 0;
            for (int y = bounded.top(); y <= bounded.bottom(); ++y)
            {
                for (int x = bounded.left(); x <= bounded.right(); ++x)
                {
                    if (image.pixelColor(x, y) == kAnnotationColor)
                    {
                        ++hits;
                    }
                }
            }
            return hits;
        }

        /// 替掉真实抓屏：只回报固定的一帧，并把交下来的屏幕名留住
        class FakeScreenSource final : public GyanisScribe::Capture::IScreenCaptureSource
        {
        public:
            explicit FakeScreenSource(Core::CapturedImage frame) : m_frame(std::move(frame))
            {
            }

            [[nodiscard]] GyanisScribe::Core::Expected<Core::CapturedImage> grabScreen(const QString &screenName) const override
            {
                requestedScreen = screenName;
                return m_frame;
            }

            [[nodiscard]] GyanisScribe::Core::Expected<Core::CapturedImage> grabWindow(quintptr) const override
            {
                return std::unexpected(Core::Error{Core::ErrorCode::PlatformUnsupported, "这条流程不抓窗口"});
            }

            mutable QString requestedScreen; ///< 屏幕名有没有原样传到底，用例据此判

        private:
            Core::CapturedImage m_frame; ///< 与生产同形状的那一帧假屏
        };

        class ModuleFlowFixture : public ::testing::Test
        {
        protected:
            static void SetUpTestSuite()
            {
                // 贴图要真的建窗口：把这一档跑到离屏平台上，测试不在用户的桌面上闪现，
                // 也和容器里那一条（env.sh 里同样 export QT_QPA_PLATFORM=offscreen）用同一个读数环境
                qputenv("QT_QPA_PLATFORM", "offscreen");
                application = std::make_unique<QApplication>(argumentCount, arguments);
                QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
                QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
                integration = GyanisScribe::Platform::createPlatformIntegration();
            }

            static void TearDownTestSuite()
            {
                integration.reset();
                application.reset();
            }

            void SetUp() override
            {
                const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
                m_root              = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Flow-{}", caseName);
                std::filesystem::remove_all(m_root);
                std::filesystem::create_directories(m_root);
                m_iniFile = m_root / "settings.ini";
            }

            void TearDown() override
            {
                std::error_code ignored;
                std::filesystem::remove_all(m_root, ignored);
                // 贴图布局用例写进的是真实的缓存目录（不在 m_root 里）：在这儿清，用例中途断言失败也清得到
                std::filesystem::remove_all(Core::Paths::cacheDirectory() / "Pins", ignored);
            }

            /**
             * @brief 走一遍抓帧入口，把 captured 信号里的帧取回来
             * @param settings 配置存储（CaptureService 要读写「上次区域」那两项）
             * @param source 假屏来源
             * @return 交出来的帧；没交出来时返回空帧并留下失败
             */
            [[nodiscard]] Core::CapturedImage captureOneFrame(GyanisScribe::Settings::SettingsStore &settings, FakeScreenSource &source) const
            {
                GyanisScribe::Capture::CaptureService service(source, settings, *integration);
                std::optional<Core::CapturedImage>    received;
                QObject::connect(&service, &GyanisScribe::Capture::ICaptureService::captured, &service, [&received](const Core::CapturedImage &image) { received = image; });

                service.captureFullScreen(QString::fromLatin1(kFakeScreenName));
                EXPECT_TRUE(received.has_value()) << "captureFullScreen 没把帧从 captured 信号交出来";
                EXPECT_EQ(source.requestedScreen, QString::fromLatin1(kFakeScreenName)) << "屏幕名没原样传到底";
                return received.value_or(Core::CapturedImage{});
            }

            std::filesystem::path m_root;    ///< 每条用例自己的临时目录，避免并行时互相踩
            std::filesystem::path m_iniFile; ///< 配置存储落在的位置

            /// 平台能力：套间级共用一份，用例结束时随 TearDownTestSuite 交还
            inline static std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> integration;
        };

        /**
         * @brief 钉住「抓帧 → 标注 → 贴图」这条链上的坐标系交接
         * @details 三处读数必须互相指同一点：帧是物理 900×600 且倍比 1.5，标注吃逻辑 50,50–250,150，
         *          导出的图形色出现在换算后的物理格上，贴图窗口的尺寸又回到逻辑 600×400。
         *          任何一环把倍比弄丢（导出没带、贴图按物理尺寸开窗），这里就会红。
         */
        TEST_F(ModuleFlowFixture, FrameKeepsItsCoordinateSystemFromCaptureToPin)
        {
            GyanisScribe::Settings::SettingsStore settings(m_iniFile);
            FakeScreenSource                      source(makeFakeFrame());

            const auto frame = captureOneFrame(settings, source);
            ASSERT_EQ(frame.image.size(), QSize(kPhysicalWidth, kPhysicalHeight)) << "抓到的帧不是物理尺寸";
            EXPECT_DOUBLE_EQ(frame.devicePixelRatio, kDevicePixelRatio);
            EXPECT_EQ(frame.logicalSize, QSize(kLogicalWidth, kLogicalHeight));
            EXPECT_EQ(frame.screenName, QString::fromLatin1(kFakeScreenName));

            GyanisScribe::Annotation::AnnotationEngine engine;
            engine.loadCanvas(frame);
            ASSERT_TRUE(engine.setActiveTool(GyanisScribe::Annotation::ToolKind::Rectangle).has_value());
            ASSERT_TRUE(engine.setPenParameters(GyanisScribe::Annotation::PenParameters{.color = kAnnotationColor, .lineWidth = 6, .fontSize = 12, .fillStyle = 0}).has_value());
            engine.beginStroke(kStrokeLogicalRect.topLeft());
            engine.moveStroke(kStrokeLogicalRect.bottomRight());
            ASSERT_TRUE(engine.endStroke().has_value()) << "矩形退化，没能进撤销栈";

            const auto exported = engine.exportImage();
            ASSERT_EQ(exported.size(), QSize(kPhysicalWidth, kPhysicalHeight)) << "导出改了物理尺寸";
            // 导出要带上倍比：贴图靠它把窗口开回逻辑尺寸，界面上的「就地比对」才对得上
            EXPECT_DOUBLE_EQ(exported.devicePixelRatio(), kDevicePixelRatio) << "导出没带倍比，贴图会按物理尺寸开窗";

            const auto strokePhysical = toPhysical(kStrokeLogicalRect);
            // 笔宽 6 逻辑格 = 9 物理格，描边压在框线两侧各约 4.5 格，再加抗锯齿的外溢；
            // 容差取 6 格：够宽到不误报「画错地方」，又窄到真画错一格就会被抓出来
            const QRect generousPhysical = strokePhysical.adjusted(-6, -6, 6, 6);
            const int   hitsInside       = countPixelsOf(exported, generousPhysical);
            const int   hitsTotal        = countPixelsOf(exported, exported.rect());
            EXPECT_GT(hitsInside, 0) << "换算后的物理格上没有图形：" << strokePhysical.x() << "," << strokePhysical.y();
            // 只许出现在换算后的位置上：整张图里框外一个都没有，才算「逻辑→物理」这一步没算错两遍或漏一遍
            EXPECT_EQ(hitsTotal, hitsInside) << "图形落在了换算位置之外：全图 " << hitsTotal << " 个，框内 " << hitsInside << " 个";

            GyanisScribe::Pin::PinManager pins(settings, *integration);
            auto                         &pin = pins.addPin(exported, QPoint(700, 300));
            EXPECT_EQ(pins.count(), 1);
            EXPECT_EQ(pin.globalRect().topLeft(), QPoint(700, 300)) << "贴图没落在请求的全局位置";
            EXPECT_EQ(pin.globalRect().size(), QSize(kLogicalWidth, kLogicalHeight)) << "贴图开成了物理尺寸：" << pin.globalRect().width() << "x" << pin.globalRect().height();

            pins.closeAll();
            EXPECT_EQ(pins.count(), 0) << "closeAll 之后还留着贴图";
        }

        /**
         * @brief 钉住「保存布局→换一个管理器实例→摆回来」这条持久化链
         * @details 布局只管几何，像素住在缓存目录里：两边任何一侧掉链子，重启后的桌面
         *          就少一张或摆回原位之外，而用户唯一的线索是「我明明贴过」。
         */
        TEST_F(ModuleFlowFixture, StoredLayoutRestoresPinsWithTheirGeometry)
        {
            GyanisScribe::Settings::SettingsStore settings(m_iniFile);
            QImage                                image(40, 24, QImage::Format_ARGB32);
            image.fill(Qt::red);

            QRect  originalRect;
            double originalScale   = 0.0;
            double originalAngle   = 0.0;
            double originalOpacity = 0.0;

            {
                GyanisScribe::Pin::PinManager pins(settings, *integration);
                auto                         &first = pins.addPin(image, QPoint(120, 80));
                first.setScale(1.5);
                first.setRotationDegrees(30.0);
                first.setOpacityLevel(0.6);
                pins.addPin(image, QPoint(300, 200));
                ASSERT_EQ(pins.count(), 2);

                // 拿原窗口最终的读数当对照：缩放与旋转绕着图片中心重排，
                // addPin 交进去的那个点早已不是左上，钉字面量就是在钉一个没人用的坐标
                originalRect    = first.globalRect();
                originalScale   = first.scale();
                originalAngle   = first.rotationDegrees();
                originalOpacity = first.opacityLevel();

                const auto saved = pins.saveLayout();
                ASSERT_TRUE(saved.has_value()) << "布局没写下去：" << saved.error().message;
            }

            GyanisScribe::Pin::PinManager reopened(settings, *integration);
            const auto                    restored = reopened.restoreLayout();
            ASSERT_TRUE(restored.has_value()) << "摆不回来：" << restored.error().message;
            EXPECT_EQ(*restored, 2) << "两张都该摆回来";
            ASSERT_EQ(reopened.count(), 2);

            const auto *first = reopened.pinAt(0);
            ASSERT_NE(first, nullptr);
            EXPECT_EQ(first->globalRect(), originalRect) << "布局没把那张图摆回同一个矩形";
            EXPECT_NEAR(first->scale(), originalScale, 1e-6) << "缩放没跟着布局回来";
            EXPECT_NEAR(first->rotationDegrees(), originalAngle, 1e-6) << "旋转没跟着布局回来";
            EXPECT_NEAR(first->opacityLevel(), originalOpacity, 1e-6) << "不透明度没跟着布局回来";
            EXPECT_EQ(reopened.pinAt(1)->globalRect().topLeft(), QPoint(300, 200)) << "恢复顺序与保存顺序不同解";
        }

        /**
         * @brief 钉住「缓存里那张图没了」只跳过那一张，而不是整次失败
         * @details 缓存目录本就是可清的东西，图片文件是用户能删的：为它放弃整批还原，
         *          等于把还在的那几张也扣掉，与用户留下的桌面差得更远。
         *          与「布局形状坏→整次失败」是两种语义，这里钉的是前一种。
         */
        TEST_F(ModuleFlowFixture, RestoreSkipsOnlyThePinWhoseImageFileIsGone)
        {
            GyanisScribe::Settings::SettingsStore settings(m_iniFile);
            QImage                                image(20, 20, QImage::Format_ARGB32);
            image.fill(Qt::green);

            {
                GyanisScribe::Pin::PinManager pins(settings, *integration);
                pins.addPin(image, QPoint(40, 40));
                pins.addPin(image, QPoint(80, 80));
                ASSERT_TRUE(pins.saveLayout().has_value()) << "布局没写下去";
            }

            const auto      cacheDirectory = Core::Paths::cacheDirectory() / "Pins";
            std::error_code ignored;
            ASSERT_TRUE(std::filesystem::remove(cacheDirectory / "pin-0.png", ignored)) << "缓存里没有那张图，用例前提就没构造出来";

            GyanisScribe::Pin::PinManager reopened(settings, *integration);
            const auto                    restored = reopened.restoreLayout();
            ASSERT_TRUE(restored.has_value()) << "少一张图不该让整次还原失败：" << restored.error().message;
            EXPECT_EQ(*restored, 1);
            ASSERT_EQ(reopened.count(), 1);
            EXPECT_EQ(reopened.pinAt(0)->globalRect().topLeft(), QPoint(80, 80)) << "跳掉坏的那一张后，还在的那张也该摆回来";
        }

        /**
         * @brief 钉住识别这一环吃的语言词表与设置页那一份同解
         * @details 设置页下拉列的是 supportedOcrLanguageTags()，后端必须只认这套标签：两边分家时
         *          界面会传出一个后端不认的标签，用户只看到「不认识这个语言」。
         *          后端不可用时必须给可操作的原因，可用时至少要走通一次完整调用——两种形状都能红。
         */
        TEST_F(ModuleFlowFixture, OcrUsesTheSharedLanguageVocabulary)
        {
            GyanisScribe::Settings::SettingsStore settings(m_iniFile);
            const auto                            engine = GyanisScribe::Ocr::createOcrEngine(settings, *integration);
            ASSERT_NE(engine, nullptr) << "选型不许交回空指针";

            const auto vocabulary = GyanisScribe::Ocr::supportedOcrLanguageTags();
            for (const auto &language: engine->supportedLanguages())
            {
                EXPECT_TRUE(vocabulary.contains(language)) << "后端给出的语言不在对外词表里：" << language.toStdString();
            }

            if (!engine->isAvailable())
            {
                const auto reason = engine->unavailableReason();
                EXPECT_FALSE(reason.isEmpty()) << "isAvailable 为假却不给原因";
                EXPECT_NE(reason.indexOf(QStringLiteral("语言")), -1) << "原因没点名语言包这件事：实际是 " << reason.toStdString();
                return;
            }

            const auto image      = makeFakeFrame().image;
            const auto recognized = engine->recognize(image, engine->supportedLanguages().front());
            ASSERT_TRUE(recognized.has_value()) << "自报可用的后端连一次完整调用都没走通：" << recognized.error().message;
            EXPECT_TRUE(vocabulary.contains(recognized->language)) << "结果里的语言不在对外词表里：" << recognized->language.toStdString();
            // 认字质量归 tests/Ocr 的用例钉（那里直接验「画上数字、认回来」）。这一档在 offscreen
            // 平台上跑，这条腿只钉接线：调用走通、语言对得上、方框落在同一套坐标里。
            for (const auto &line: recognized->lines)
            {
                EXPECT_TRUE(image.rect().contains(line.boundingBox)) << "行框跑到了图外";
                for (const auto &word: line.words)
                {
                    EXPECT_TRUE(image.rect().contains(word.boundingBox)) << "词框跑到了图外";
                }
            }
        }
    } // namespace
    /**
     * @brief 钉住「贴图开着鼠标穿透时，托盘那一条能把它们点回来」
     * @details 穿透中的贴图收不到右键，自己那一档就再也关不掉；没有这条出口时唯一的办法是
     *          「关闭全部贴图」，那等于把用户摆了一桌的成果一起丢掉。计数只算真开着的那些：
     *          一张都没恢复时说「已让 0 张…」就是把没发生的事报成发生了。
     */
    TEST_F(ModuleFlowFixture, TrayEscapeHatchMakesThroughPinsClickableAgain)
    {
        if (!integration->capabilities().clickThrough)
        {
            GTEST_SKIP() << "本平台没有鼠标穿透能力，开不出穿透中的贴图，这条出口在这里没有可复现的现场";
        }

        GyanisScribe::Settings::SettingsStore settings(m_iniFile);
        FakeScreenSource                      source(makeFakeFrame());
        const auto                            frame = captureOneFrame(settings, source);

        GyanisScribe::Pin::PinManager pins(settings, *integration);
        auto                         &through   = pins.addPin(frame.image, QPoint(700, 300));
        auto                         &clickable = pins.addPin(frame.image, QPoint(900, 300));
        ASSERT_TRUE(through.setMouseThrough(true).has_value()) << "能力位说有穿透，开起来却失败：能力表与实现不同解";
        ASSERT_TRUE(through.isMouseThrough());
        EXPECT_FALSE(clickable.isMouseThrough());

        EXPECT_EQ(pins.clearMouseThrough(), 1) << "只该恢复那一张开着穿透的贴图";
        EXPECT_FALSE(through.isMouseThrough()) << "按过之后还穿透着，这条出口就是假的";

        EXPECT_EQ(pins.clearMouseThrough(), 0) << "再按一次没有可恢复的了，不该再报出数字";

        pins.closeAll();
    }

    /**
     * @brief 钉住「托盘那一条把整桌贴图收起、再原样摆回」
     * @details 收起不等于关掉：册上张数不变，只是不在屏上，摆回时摆位与缩放都还在。
     *          计数只算这次真的改了状态的张数——已经收着的再收一次还报「2」就是把没发生的事
     *          报成发生了，而用户只能靠这句数字判断自己按到了哪一档。
     */
    TEST_F(ModuleFlowFixture, TrayToggleConcealsAndRestoresEveryPin)
    {
        GyanisScribe::Settings::SettingsStore settings(m_iniFile);
        FakeScreenSource                      source(makeFakeFrame());
        const auto                            frame = captureOneFrame(settings, source);

        GyanisScribe::Pin::PinManager pins(settings, *integration);
        auto                         &first  = pins.addPin(frame.image, QPoint(700, 300));
        auto                         &second = pins.addPin(frame.image, QPoint(900, 300));
        ASSERT_FALSE(first.isConcealed()) << "刚贴出来的图就该在桌面上";
        EXPECT_EQ(pins.visiblePinCount(), 2);

        EXPECT_EQ(pins.setAllPinsConcealed(true), 2) << "两张都该被收起，这个数是这次真的改了状态的张数";
        EXPECT_TRUE(first.isConcealed());
        EXPECT_TRUE(second.isConcealed());
        EXPECT_EQ(pins.visiblePinCount(), 0);
        EXPECT_EQ(pins.count(), 2) << "收起不是关掉：册上还是两张，摆位也还在";

        EXPECT_EQ(pins.setAllPinsConcealed(true), 0) << "已经收着的再收一次不该再报出数字";
        EXPECT_EQ(pins.setAllPinsConcealed(false), 2);
        EXPECT_FALSE(first.isConcealed());
        EXPECT_FALSE(second.isConcealed());
        EXPECT_EQ(pins.visiblePinCount(), 2);

        pins.closeAll();
    }

    /**
     * @brief 钉住拖放这条路从内容到成品的整段：图片文件贴出、多件错开、像素真的对
     * @details 借剪贴板把贴出来的像素读回来（窗口的复制与绘制读同一份图）。
     *          渲染与「把剪贴板里的内容贴到屏幕上」共用同一套，两处不会将来只改一处。
     */
    TEST_F(ModuleFlowFixture, DroppedImageFilesBecomePinsCascadingFromTheDropPoint)
    {
        const auto firstPath  = m_root / "first.png";
        const auto secondPath = m_root / "second.png";
        QImage     firstImage{QSize{8, 8}, QImage::Format_ARGB32};
        firstImage.fill(QColor(255, 0, 0));
        QImage secondImage = firstImage;
        secondImage.fill(QColor(0, 0, 255));
        ASSERT_TRUE(firstImage.save(QString::fromStdString(firstPath.string())));
        ASSERT_TRUE(secondImage.save(QString::fromStdString(secondPath.string())));

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(QString::fromStdString(firstPath.string())), QUrl::fromLocalFile(QString::fromStdString(secondPath.string()))});

        Settings::SettingsStore settings(m_iniFile);
        Pin::PinManager         pins(settings, *integration);
        const auto              dropped = App::pinDroppedContent(pins, &mime, QPoint(400, 300), QApplication::font());
        ASSERT_TRUE(dropped.has_value()) << dropped.error().message;
        EXPECT_EQ(dropped->pinnedCount, 2);
        ASSERT_EQ(pins.count(), 2);

        ASSERT_NE(pins.pinAt(0), nullptr);
        EXPECT_EQ(pins.pinAt(0)->globalRect().topLeft(), QPoint(400, 300)) << "第一张没落在拖放点上";
        ASSERT_NE(pins.pinAt(1), nullptr);
        EXPECT_EQ(pins.pinAt(1)->globalRect().topLeft(), QPoint(424, 324)) << "第二张该往右下错开一格：完全叠住会让人以为只贴上一张";

        pins.pinAt(0)->copyToClipboard();
        EXPECT_EQ(QGuiApplication::clipboard()->image().pixelColor(4, 4), QColor(255, 0, 0)) << "贴出来的像素不是盘上那张文件";
        pins.closeAll();
        EXPECT_EQ(pins.count(), 0);
    }

} // namespace GyanisScribe
