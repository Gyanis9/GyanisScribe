/**
 * @file ShareDialog.h
 * @brief 美化分享对话框：实时预览、模板选择、自定义模板增删与复制/另存
 * @author Gyanis
 * @date 2026-11-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Capture/SourceInfo.h"
#include "Core/Share/ShareStyle.h"

#include <QDialog>
#include <QImage>
#include <QString>

#include <filesystem>
#include <vector>

class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QResizeEvent;
class QSlider;
class QTimer;

namespace GyanisScribe::Core
{
    class ISettingsStore;
}

namespace GyanisScribe::Gui
{

    class Button;

    /**
     * @brief 美化分享对话框
     * @details 左侧预览、右侧样式表单；内置模板与本地自定义模板共用一份下拉，
     *          当前选择写回 Share/LastTemplate。预览按 1200 像素宽上限渲染并防抖，
     *          改控件只更新内存样式，不动原图与标注现场；复制与另存都按原尺寸重新渲染。
     */
    class ShareDialog : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造对话框
         * @param image 标注成品，像素
         * @param source 这一轮的截图来源
         * @param settings 配置存储，模板与上次选择都从这里读写
         * @param parent 父窗口，可空
         */
        ShareDialog(QImage image, Core::CaptureSourceInfo source, Core::ISettingsStore &settings, QWidget *parent = nullptr);

        /**
         * @brief 取当前样式
         * @return 内存里正在预览的样式
         */
        [[nodiscard]] const Core::ShareStyle &currentStyle() const noexcept;

        /**
         * @brief 取当前选中的模板 id
         * @return 内置固定 id 或 custom- 前缀的自定义 id
         */
        [[nodiscard]] QString selectedTemplateId() const;

        /**
         * @brief 取最近一次渲染的预览图
         * @return 宽不超过 1200 像素的成品预览；还没渲染成功时为空
         */
        [[nodiscard]] QImage previewImageForTesting() const;

        /**
         * @brief 取构造时收下的截图来源
         * @return 来源元数据
         */
        [[nodiscard]] const Core::CaptureSourceInfo &sourceForTesting() const noexcept;

        /**
         * @brief 取状态行当前文字
         * @return 最近一次操作的结果或错误原因
         */
        [[nodiscard]] QString statusText() const;

        /**
         * @brief 按原尺寸渲染并放进系统剪贴板
         * @return 成功返回空载荷；渲染失败时返回原因，状态行同时点亮
         * @details 剪贴板里拿到的始终是整尺寸成品，而不是左侧那张预览缩略图。
         */
        [[nodiscard]] Base::Expected<void> copyToClipboard();

        /**
         * @brief 按原尺寸渲染并另存为图片
         * @param filePath 目标文件，后缀决定格式：png、jpg、jpeg 或 webp
         * @return 成功返回空载荷；格式不支持、渲染失败、编码失败或写盘失败时返回原因，状态行同时点亮
         * @details 写盘走 QSaveFile 原子替换：中途失败不会在盘上留下半张图。有损档固定按
         *          kDefaultImageQuality 写，不读设置里的保存质量；不写历史，也不跑截图后的外部命令。
         */
        [[nodiscard]] Base::Expected<void> saveToFile(const std::filesystem::path &filePath);

        /**
         * @brief 给「保存图片…」对话框准备默认路径
         * @return 设置里的截图目录（没填时系统图片目录下的 GyanisScribe）加按模板与来源生成的文件名，后缀 .png
         * @details 名字走与截图保存同一套模板与去重：同一秒里连按两次保存不会默认指向同一个文件。
         */
        [[nodiscard]] QString suggestedFilePath() const;

        /**
         * @brief 校正「保存图片…」对话框交回的后缀
         * @param chosen 用户在文件对话框里确认的路径
         * @return 后缀是 png、jpg、jpeg 或 webp 时原样返回；其余情况（含没有后缀）在末尾补 .png
         * @details 只服务文件对话框这一路：saveToFile 仍按调用方给的后缀决定格式并报错，不替调用方改名。
         */
        [[nodiscard]] static std::filesystem::path savePathForDialog(const std::filesystem::path &chosen);

        /**
         * @brief 把当前样式另存为自定义模板
         * @param name 模板名，首尾空白会被去掉
         * @return 成功返回空载荷；名称为空、过长、与现有或内置模板重名、样式无效或写设置失败时返回原因
         */
        [[nodiscard]] Base::Expected<void> saveTemplateAs(const QString &name);

        /**
         * @brief 删除当前选中的自定义模板
         * @return 成功返回空载荷并回落到 light；当前是内置模板或写设置失败时返回原因
         */
        [[nodiscard]] Base::Expected<void> deleteSelectedTemplate();

        /**
         * @brief 停掉预览防抖并立刻重渲染
         */
        void flushPreviewForTesting();

    protected:
        /**
         * @brief 窗口尺寸变化时按保存的预览图重新缩放
         * @param event 尺寸变化事件
         * @details 只缩放已有像素，不重做全尺寸渲染：拖动窗口不该反复走一遍整张画布的绘制
         */
        void resizeEvent(QResizeEvent *event) override;

        /**
         * @brief 预览标签尺寸落位后按保存的预览图重新缩放
         * @param watched 事件接收者
         * @param event 事件
         * @return 已处理返回 true，其余交回基类
         * @details 顶层窗口的 resizeEvent 早于布局把新尺寸交给子控件，标签自己的 resize 才是缩放的准点
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        Q_DISABLE_COPY(ShareDialog)

        /// 搭出左侧预览与右侧表单
        void buildUi();
        /// 从设置读自定义模板并重建下拉
        void loadTemplates();
        /// 把内存样式灌进各控件
        void applyStyleToControls();
        /// 把控件当前取值收回内存样式
        void applyControlsToStyle();
        /// 启动 50 毫秒的单次预览定时器
        void schedulePreview();
        /// 按 1200 像素宽上限重渲染预览
        void renderPreview();
        /// 按当前是否内置模板更新删除按钮的可用状态
        void updateTemplateActions();
        /// 选中一套模板并把选择写回设置
        void selectTemplate(const QString &id);
        /// 读 Share/LastTemplate，指向已不存在的模板时回到 light
        [[nodiscard]] QString persistedLastTemplateId() const;
        /// 设状态行文字
        void setStatus(const QString &text, bool error);

        QImage                           m_image;    ///< 标注成品，渲染与后续导出共用
        Core::CaptureSourceInfo          m_source;   ///< 这一轮的来源元数据
        Core::ISettingsStore            &m_settings; ///< 配置存储，模板与上次选择都走它
        std::vector<Core::ShareTemplate> m_custom;   ///< 设置里的自定义模板
        Core::ShareStyle                 m_style;    ///< 内存里正在预览的样式

        QLabel      *m_preview           = nullptr; ///< 左侧预览
        QComboBox   *m_templateCombo     = nullptr; ///< 模板下拉
        Button      *m_backgroundButton  = nullptr; ///< 背景色按钮
        Button      *m_secondColorButton = nullptr; ///< 渐变终点色按钮
        QCheckBox   *m_gradient          = nullptr; ///< 渐变开关
        QSlider     *m_padding           = nullptr; ///< 内边距滑杆
        QSlider     *m_radius            = nullptr; ///< 圆角滑杆
        QSlider     *m_borderWidth       = nullptr; ///< 描边宽度滑杆
        QSlider     *m_shadowRadius      = nullptr; ///< 投影宽度滑杆
        QCheckBox   *m_editorChrome      = nullptr; ///< 代码窗口开关
        QLineEdit   *m_editorTitle       = nullptr; ///< 标题栏文字
        QLineEdit   *m_watermarkText     = nullptr; ///< 水印文字
        QSlider     *m_watermarkOpacity  = nullptr; ///< 水印不透明度
        QComboBox   *m_watermarkCorner   = nullptr; ///< 水印位置
        QPushButton *m_saveTemplate      = nullptr; ///< 另存为模板
        QPushButton *m_deleteTemplate    = nullptr; ///< 删除模板
        QPushButton *m_copyButton        = nullptr; ///< 复制到剪贴板
        QPushButton *m_saveFileButton    = nullptr; ///< 保存图片
        QPushButton *m_closeButton       = nullptr; ///< 关闭对话框
        QLabel      *m_statusLabel       = nullptr; ///< 状态行

        QTimer *m_previewTimer = nullptr; ///< 预览防抖
        QImage  m_previewImage;           ///< 最近一次渲染的预览图
        QString m_status;                 ///< 状态行文字
    };

} // namespace GyanisScribe::Gui
