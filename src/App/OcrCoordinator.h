/**
 * @file OcrCoordinator.h
 * @brief 文字识别一条链的协作对象：选后端 → 收图 → 交给搬运工 → 把结果摆进窗口
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Gui/Controls/Toast.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

#include <filesystem>
#include <memory>

namespace GyanisScribe::Core
{
    class IOcrEngine;
    class ISettingsStore;
    class RecognitionRunner;
    struct OcrResult;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class IconProvider;
    class OcrResultWindow;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{

    /**
     * @brief 识别这一族从控制器里搬出来的落脚处（审查台账 A-11）
     * @details 它只认「有人给了一张图要认」：不起线程、不碰截图、不碰托盘。
     *          要出到界面上的话（后端没编进来、没有可认的图、认失败了）走 messageForUser 交回控制器，
     *          与窗口那一族的做法一致——识别链自己不知道提示长在哪儿。
     */
    class OcrCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param settings 配置来源：选哪个后端、记住上一次的语言、结果窗的文件名模板
         * @param integration 平台能力：后端选型要看这台机器有没有系统识别
         * @param icons 图标供给器，结果窗用
         * @param theme 主题来源，结果窗用
         * @param parent Qt 父对象
         */
        OcrCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, GyanisScribe::Gui::IconProvider &icons,
                       GyanisScribe::Gui::ThemeManager &theme, QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：两个成员的唯一指针各指着一个只前向声明的类型
         *       （引擎与搬运工），析构要在类型完整的 TU 里实例化——否则别的 TU 里
         *       内联析构会让 clang/GCC 报 sizeof 应用在不完整类型上（MSVC 默认放过，口径不一）。
         */
        ~OcrCoordinator() override;

        /**
         * @brief 给一张要认的图：首次识别、截图后的自动识别、贴图与标注窗的「识别文字」都走这里
         * @param image 待识别的图
         * @details 语言取上一次的选择：换过一次就该记住，而不是每轮都退回默认档。
         */
        void requestRecognition(const QImage &image);

        /**
         * @brief 认盘上的一个文件：历史页的「重新识别」与托盘的「识别图片文件…」共用这一份实现
         * @param filePath 图片路径
         */
        void recognizeFile(const std::filesystem::path &filePath);

        /// 先让用户挑一张盘上的图，再交给 recognizeFile；取消时什么都不做
        void pickImageFileAndRecognize();

        /**
         * @brief 用给定语言认一次当前这张图，并把结果摆进结果窗口
         * @param language 语言标签，如 zh-Hans
         * @details 首次识别与窗口里「换语言/重新识别」走同一条路：两条路各写一遍认字，
         *          就会出现一个能提示「后端没编进来」而另一个静默给空结果。
         */
        void recognizeWithLanguage(const QString &language);

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）：识别这条链不自己弹提示
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    private:
        /**
         * @brief 工作线程认完之后把结果摆进结果窗口
         * @param image 请求时那一张，方框与它同系；期间用户重新截的图不算这一张
         * @param language 请求时那门语言，认成之后才记住它
         * @param result 识别结果，空结果也照常开窗
         * @details 与 handleFailed 分两条槽而不是一条带 Expected：这里两个分支各写各的文案，
         *          合在一条里就要在槽里再判一次有没有值，而搬运工已经判过了。
         */
        void handleSucceeded(const QImage &image, const QString &language, const GyanisScribe::Core::OcrResult &result);

        /**
         * @brief 识别失败：把后端给的中文原因原样报给用户
         * @param error 后端给的故障类别与文案
         */
        void handleFailed(const GyanisScribe::Base::Error &error);

        GyanisScribe::Core::ISettingsStore                    &m_settings; ///< 语言与文件名模板的来源，不拥有
        GyanisScribe::Gui::IconProvider                       &m_icons;    ///< 结果窗的图标出口，不拥有
        GyanisScribe::Gui::ThemeManager                       &m_theme;    ///< 结果窗的主题来源，不拥有
        std::unique_ptr<GyanisScribe::Core::IOcrEngine>        m_ocr;      ///< 识别后端，按设置与平台能力选一次
        std::unique_ptr<GyanisScribe::Core::RecognitionRunner> m_runner;   ///< 识别的线程搬运工；它拿着引擎的引用，所以必须排在 m_ocr 之后析构
        QImage                                                 m_image;    ///< 最近一次送去识别的图，换语言重认按它算
        QPointer<GyanisScribe::Gui::OcrResultWindow>           m_window;   ///< 识别结果窗口，关了就重建，不拥有
    };

} // namespace GyanisScribe::App
