#include "Platform/UiElementLabels.h"

#include "Base/Text.h"

namespace GyanisScribe::Platform
{

    QString uiControlTypeLabel(const int uiaControlTypeId)
    {
        // 数值取自 UIAutomationClient.h 的 UIA_*ControlTypeId；常量名留作对照，
        // 这里不引用那份头，非 Windows 的构建也要编得到这一份
        switch (uiaControlTypeId)
        {
            case 50000:
                return Base::uiText("按钮"); // UIA_ButtonControlTypeId
            case 50001:
                return Base::uiText("日历"); // UIA_CalendarControlTypeId
            case 50002:
                return Base::uiText("复选框"); // UIA_CheckBoxControlTypeId
            case 50003:
                return Base::uiText("下拉框"); // UIA_ComboBoxControlTypeId
            case 50004:
                return Base::uiText("编辑框"); // UIA_EditControlTypeId
            case 50005:
                return Base::uiText("链接"); // UIA_HyperlinkControlTypeId
            case 50006:
                return Base::uiText("图片"); // UIA_ImageControlTypeId
            case 50007:
                return Base::uiText("列表项"); // UIA_ListItemControlTypeId
            case 50008:
                return Base::uiText("列表"); // UIA_ListControlTypeId
            case 50009:
                return Base::uiText("菜单"); // UIA_MenuControlTypeId
            case 50010:
                return Base::uiText("菜单栏"); // UIA_MenuBarControlTypeId
            case 50011:
                return Base::uiText("菜单项"); // UIA_MenuItemControlTypeId
            case 50012:
                return Base::uiText("进度条"); // UIA_ProgressBarControlTypeId
            case 50013:
                return Base::uiText("单选框"); // UIA_RadioButtonControlTypeId
            case 50014:
                return Base::uiText("滚动条"); // UIA_ScrollBarControlTypeId
            case 50015:
                return Base::uiText("滑块"); // UIA_SliderControlTypeId
            case 50016:
                return Base::uiText("微调框"); // UIA_SpinnerControlTypeId
            case 50017:
                return Base::uiText("状态栏"); // UIA_StatusBarControlTypeId
            case 50018:
                return Base::uiText("标签页"); // UIA_TabControlTypeId
            case 50019:
                return Base::uiText("标签项"); // UIA_TabItemControlTypeId
            case 50020:
                return Base::uiText("文本"); // UIA_TextControlTypeId
            case 50021:
                return Base::uiText("工具栏"); // UIA_ToolBarControlTypeId
            case 50022:
                return Base::uiText("提示"); // UIA_ToolTipControlTypeId
            case 50023:
                return Base::uiText("树"); // UIA_TreeControlTypeId
            case 50024:
                return Base::uiText("树项"); // UIA_TreeItemControlTypeId
            case 50025:
                return Base::uiText("控件"); // UIA_CustomControlTypeId
            case 50026:
                return Base::uiText("分组"); // UIA_GroupControlTypeId
            case 50027:
                return Base::uiText("缩略图"); // UIA_ThumbControlTypeId
            case 50028:
                return Base::uiText("表格"); // UIA_DataGridControlTypeId
            case 50029:
                return Base::uiText("表格项"); // UIA_DataItemControlTypeId
            case 50030:
                return Base::uiText("文档"); // UIA_DocumentControlTypeId
            case 50031:
                return Base::uiText("拆分按钮"); // UIA_SplitButtonControlTypeId
            case 50032:
                return Base::uiText("窗口"); // UIA_WindowControlTypeId
            case 50033:
                return Base::uiText("面板"); // UIA_PaneControlTypeId
            case 50034:
                return Base::uiText("表头"); // UIA_HeaderControlTypeId
            case 50035:
                return Base::uiText("表头项"); // UIA_HeaderItemControlTypeId
            case 50036:
                return Base::uiText("表格"); // UIA_TableControlTypeId
            case 50037:
                return Base::uiText("标题栏"); // UIA_TitleBarControlTypeId
            case 50038:
                return Base::uiText("分隔线"); // UIA_SeparatorControlTypeId
            default:
                return Base::uiText("控件");
        }
    }

} // namespace GyanisScribe::Platform
