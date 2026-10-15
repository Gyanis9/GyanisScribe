/**
 * @file OcrResultWindow.h
 * @brief 识别结果窗口：左边原图预览、右边文本，点一行文本就在图上圈出那一行
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Ocr/OcrTypes.h"
#include "Gui/Controls/Toast.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <optional>

class QLabel;
class QComboBox;
class QResizeEvent;
class QTextEdit;

namespace GyanisScribe::Gui
{
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Gui
{

    /**
     * @brief 文本控件的行号换算成结果里的行下标
     * @param result 当前识别结果
     * @param blockIndex QTextEdit 的块序号，从 0 开始
     * @return 行下标；块序号落在结果之外时返回 -1
     * @details 返回 -1 而不是夹到边缘：光标停在一个不存在的位置时，把别的地方的方框圈起来
     *          比什么都不圈更容易让人以为识别错了。
     */
    [[nodiscard]] int lineIndexForBlock(const Core::OcrResult &result, int blockIndex);

    /**
     * @brief 算某一行在原图预览里的方框
     * @param result 当前识别结果
     * @param lineIndex 行下标
     * @param imageSize 原图尺寸，像素
     * @param previewSize 预览控件尺寸，逻辑像素
     * @return 预览控件坐标下的方框；行不存在、行框为空或尺寸不成立时返回空
     */
    [[nodiscard]] std::optional<QRect> highlightRectFor(const Core::OcrResult &result, int lineIndex, const QSize &imageSize, const QSize &previewSize);

    /**
     * @brief 结果摘要那一行的文本
     * @param result 当前识别结果
     * @param imageSize 原图尺寸，像素
     * @return 「N 行 · 平均置信度 P% · 宽×高」；没有行时报「没认出文字」
     * @details 置信度按百分数取整给出：0.87 这种小数在界面上要数一遍位数才知道是高还是低。
     *          后端压根不报这一路读数时那一格写「未提供」，不打 0%。
     */
    [[nodiscard]] QString statusLineFor(const Core::OcrResult &result, const QSize &imageSize);

    /**
     * @brief 识别结果窗口
     * @details 窗口自己不认字：重新识别与换语言都只发一个 recognizeRequested 信号，
     *          由持有引擎的调用方去做，这样窗口在没有后端的构建里也能照常展示已有结果。
     *          导出走 OcrTextExporter，路径由用户选，失败就地提示而不清空已有文本。
     */
    class OcrResultWindow : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造结果窗口
         * @param image 识别用的原图，窗口按等比缩放显示
         * @param result 识别结果
         * @param icons 图标供给器，工具栏按钮的图标由它取
         * @param theme 主题管理器，决定高亮框用哪个主色
         * @param nameTemplate 导出时的文件名模板；留空表示默认档（与截图成品同一套名字规则）
         * @param parent 父窗口，可空
         */
        OcrResultWindow(const QImage &image, const Core::OcrResult &result, Gui::IconProvider &icons, const Gui::ThemeManager &theme, const QString &nameTemplate = {},
                        QWidget *parent = nullptr);

        /**
         * @brief 换一份结果与底图（重新识别之后调用）
         * @param image 新的原图，与 result 的方框同一坐标系
         * @param result 新的识别结果
         * @details 语言下拉保留用户当前选择：重新识别不该把刚选的语言抹掉。
         */
        void setResult(const QImage &image, const Core::OcrResult &result);

        /**
         * @brief 当前选中的语言标签
         * @return 下拉框里的标签，如 zh-Hans
         */
        [[nodiscard]] QString selectedLanguage() const;

        /**
         * @brief 复制全部文本到剪贴板
         * @return 实际写进剪贴板的文本；结果为空时返回空串
         */
        QString copyAllToClipboard();

        /**
         * @brief 复制文本框里选中的部分
         * @return 选中并写进剪贴板的文本；没有选区时返回空串
         */
        QString copySelectionToClipboard();

        /**
         * @brief 导出 TXT
         * @param filePath 目标文件，已存在则覆盖
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         */
        [[nodiscard]] Base::Expected<void> exportTxt(const std::filesystem::path &filePath);

        /**
         * @brief 导出 JSON
         * @param filePath 目标文件，已存在则覆盖
         * @return 成功返回空载荷；失败返回带路径与原因的 Error
         */
        [[nodiscard]] Base::Expected<void> exportJson(const std::filesystem::path &filePath);

        /// 状态摘要那一行的文本，供用例与外部提示读取
        [[nodiscard]] QString statusText() const;

    signals:
        /**
         * @brief 用户要求用另一个语言重新识别
         * @param language 语言标签
         */
        void recognizeRequested(const QString &language);

        /**
         * @brief 复制、导出等动作给人读的一条提示
         * @param message 中文正文，含原因与下一步
         * @param kind 语义种类：需求写定成功用绿、错误用危险色，由持有气泡的调用方决定落在哪
         */
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    protected:
        /**
         * @brief 窗口改尺寸后重算高亮方框
         * @details 预览区的方框是按当前控件尺寸算的：不跟着重算，拖动窗口之后圈的位置会整体偏，
         *          而那看起来像「识别错了行」。
         * @param event 尺寸变化事件，本层不读它的内容
         */
        void resizeEvent(QResizeEvent *event) override;

    private:
        /// 预览控件的绘制与选行刷新
        void refreshPreview();

        /// 问用户要路径并导出 TXT；取消对话框不提示
        void chooseAndExportTxt();

        /// 问用户要路径并导出 JSON；取消对话框不提示
        void chooseAndExportJson();

        Q_DISABLE_COPY(OcrResultWindow)

        QImage                   m_image;               ///< 原图，预览按它算方框
        Core::OcrResult          m_result;              ///< 当前结果，窗口只展示不改写
        Gui::IconProvider       &m_icons;               ///< 图标供给器
        const Gui::ThemeManager &m_theme;               ///< 主题管理器，取主色画高亮框
        QString                  m_nameTemplate;        ///< 导出用文件名模板，构造时给；空串表示默认档
        QWidget                 *m_preview   = nullptr; ///< 左侧预览控件，重绘由 refreshPreview 驱动
        QTextEdit               *m_text      = nullptr; ///< 右侧文本，只读、可选中
        QLabel                  *m_status    = nullptr; ///< 摘要那一行，随结果刷新
        QComboBox               *m_language  = nullptr; ///< 语言下拉，取值来自后端词表
        int                      m_lineIndex = -1;      ///< 当前定位的行下标，-1 表示不圈任何行
        std::optional<QRect>     m_highlight;           ///< 当前行在预览里的方框
    };

} // namespace GyanisScribe::Gui
