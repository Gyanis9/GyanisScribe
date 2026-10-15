/**
 * @file PinGroupManagerWindow.h
 * @brief 贴图组管理窗：一屏看全桌「哪几组、各有几张、叫什么」，就地摆出某一组、改名、腾空与删组
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace GyanisScribe::Gui
{
    class PinManager;

    /**
     * @brief 贴图组的管理窗
     * @details 右键子菜单一次只能管一组、还得先有一张贴图在手边；这一扇给的是「全桌的账」：
     *          哪几组在册、各有几张、哪一组正摆在桌上。四个动作全部走 PinManager 的公开出口，
     *          窗口自己不留状态——每次刷新重读一遍，才不会出现「别处改了、这里还挂着旧名字」。
     */
    class PinGroupManagerWindow : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造并接线
         * @param pins 贴图管理器，读数与动作都落在它身上；生命周期必须长于本窗口
         * @param parent Qt 父对象
         */
        explicit PinGroupManagerWindow(PinManager &pins, QWidget *parent = nullptr);

        /**
         * @brief 析构：表里的行与按钮由 Qt 收回
         */
        ~PinGroupManagerWindow() override;

        /**
         * @brief 摆出这扇窗：先重读一遍各组概况，再抬到前面
         */
        void openAndRefresh();

        /**
         * @brief 表上真摆出来的那一份（每行「组号|显示名|张数|是否当前」）
         * @return 行序就是组号升序
         * @details 判据要读的是界面端的那一份：只验管理器算出的清单，会漏掉「名字显示成了什么」这一层。
         */
        [[nodiscard]] QStringList tableReadout() const;

        /// 底部那行提示现在的文字（四个动作的回声与拒绝原因都写在这儿）
        [[nodiscard]] QString hintText() const;

        /**
         * @brief 「摆出这一组」那一步：把活动组换成点名那一组
         * @param group 组号；0 表示没有点名（按钮在没选中时被按住不响，此处同样不动）
         * @details 四个动作都做成「点名一个组号」的公开方法：按钮与输入框都要人点，判据钉不到那一步上，
         *          而它们干的正是这四件事——把「拿组号」与「做事」分开，两边才都能单独验。
         */
        void showGroupRow(int group);

        /**
         * @brief 「重命名」那一步：把名字写给点名那一组
         * @param group 组号，0 不动
         * @param name 新名字；首尾空白由管理器剪掉，全空就是取消命名
         */
        void renameGroupRow(int group, const QString &name);

        /**
         * @brief 「腾空这一组」那一步：关掉点名那一组在册的全部贴图
         * @param group 组号，0 不动
         */
        void voidGroupRow(int group);

        /**
         * @brief 「删掉这一组」那一步：成员并回第一组，名字跟着清
         * @param group 组号，0 不动
         */
        void deleteGroupRow(int group);

    private:
        /// 重读各组概况并把表重摆一遍
        void refresh();

        /// 选中那行的组号；没选中返回 0
        [[nodiscard]] int selectedGroup() const;

        /// 按「有没有选中一行」定四个按钮的可用态
        void updateButtonStates();

        /// 开输入框拿名字，再交给 renameGroupRow
        void askAndRenameSelectedGroup();

        /// 把一句结果写在底部那行提示上
        void say(const QString &message);

        PinManager   &m_pins;                   ///< 贴图管理器，不拥有
        QTableWidget *m_table        = nullptr; ///< 三列表：分组 / 张数 / 摆在桌上
        QLabel       *m_hint         = nullptr; ///< 底部那行提示
        QPushButton  *m_showButton   = nullptr; ///< 「摆出这一组」
        QPushButton  *m_renameButton = nullptr; ///< 「重命名…」
        QPushButton  *m_voidButton   = nullptr; ///< 「腾空这一组」
        QPushButton  *m_deleteButton = nullptr; ///< 「删掉这一组」
        QPushButton  *m_closeButton  = nullptr; ///< 关掉这扇窗
    };

} // namespace GyanisScribe::Gui
