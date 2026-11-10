/**
 * @file ShareDialog.h
 * @brief 美化分享对话框：实时预览、模板选择与自定义模板增删
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

#include <vector>

class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSlider;
class QTimer;

namespace GyanisScribe::Core
{
    class ISettingsStore;
}

namespace GyanisScribe::Gui
{

    /**
     * @brief 美化分享对话框
     * @details 左侧预览、右侧样式表单；内置模板与本地自定义模板共用一份下拉，
     *          当前选择写回 Share/LastTemplate。预览按 1200 像素宽上限渲染并防抖，
     *          改控件只更新内存样式，不动原图与标注现场。
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
        QPushButton *m_backgroundButton  = nullptr; ///< 背景色按钮
        QPushButton *m_secondColorButton = nullptr; ///< 渐变终点色按钮
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
        QLabel      *m_statusLabel       = nullptr; ///< 状态行

        QTimer *m_previewTimer = nullptr; ///< 预览防抖
        QImage  m_previewImage;           ///< 最近一次渲染的预览图
        QString m_status;                 ///< 状态行文字
    };

} // namespace GyanisScribe::Gui
