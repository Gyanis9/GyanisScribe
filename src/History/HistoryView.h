/**
 * @file HistoryView.h
 * @brief 历史页：缩略图列表、搜索与类别筛选，以及「重新贴图 / 重新识别 / 删除」三个出口
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "History/HistoryRules.h"
#include "History/HistoryStore.h"
#include "History/HistoryTypes.h"

#include "Gui/Controls/Toast.h"

#include <QStringList>
#include <QVector>
#include <QWidget>

#include <filesystem>
#include <optional>

class QLineEdit;
class QListWidget;
class QComboBox;

namespace GyanisScribe::Gui
{
    class IconProvider;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::History
{

    /**
     * @brief 一条历史在列表里的文字
     * @param entry 记录
     * @param now 判「今天 / 昨天」用的时刻，用例里由调用方给
     * @return 「今天 14:03 · 截图 · 800×600」这样的单行文本；尺寸读不出时不带最后那一格
     * @details 相对日期放在最前：用户找的是「今天上午那几张」，绝对日期要先心算一次。
     *          尺寸为空是「这一轮没落下帧」这类读不出的现场，硬打 0×0 等于把不知道报成一个数。
     */
    [[nodiscard]] QString entryCaption(const HistoryEntry &entry, const QDateTime &now);

    /**
     * @brief 筛选下拉的项文本，顺序与 kindFilterAt 互指
     * @return 「全部」「截图」「录屏」三项
     */
    [[nodiscard]] QStringList historyFilterLabels();

    /**
     * @brief 筛选下拉的一档换成类别
     * @param index 下拉序号
     * @return 类别；第 0 档表示不限类别，序号越界也按不限处理
     */
    [[nodiscard]] std::optional<HistoryKind> kindFilterAt(int index);

    /**
     * @brief 按搜索词与类别筛出一页记录
     * @param entries 全量记录（时间倒序）
     * @param query 搜索词，空串表示不过滤文本
     * @param kindFilter 类别，空表示不限
     * @return 保持原次序的可见记录
     */
    [[nodiscard]] QVector<HistoryEntry> visibleEntries(const QVector<HistoryEntry> &entries, const QString &query, const std::optional<HistoryKind> &kindFilter);

    /**
     * @brief 历史页控件
     * @details 视图不认贴图也不认识识别：三个出口只发信号，路径交给持有各模块的控制器。
     *          缩略图现读现裁，不另存一份缓存——多存一份就要管它什么时候过期。
     */
    class HistoryView : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造历史页
         * @details 收尾读一次清单：这一页第一次摆出来就得是当前的记录，不能等下一次保存才有内容。
         * @param store 历史清单，刷新时从它读
         * @param policy 保留策略，读清单时裁剪
         * @param icons 图标供给器，读不到缩略图时的兜底图标由它给
         * @param parent 父控件
         * @note 主题不进构造：列表配色全走 QSS，这里没有需要现取的主题色，多接一个引用就是死字段
         */
        HistoryView(HistoryStore &store, const HistoryRetentionPolicy &policy, Gui::IconProvider &icons, QWidget *parent = nullptr);

        /**
         * @brief 重读清单并按当前条件重填列表
         * @details 视图建立时读过一次，之后每次截图或录屏由调用方触发；列表不会自己发现盘上多了文件。
         */
        void refresh();

        /**
         * @brief 当前列出的条数
         * @return 列表项数量，用例据此判断筛选与刷新有没有生效
         */
        [[nodiscard]] int rowCount() const;

        /**
         * @brief 第一行的文字
         * @return 一条记录文案；列表为空时返回空串
         */
        [[nodiscard]] QString firstCaption() const;

        /**
         * @brief 选中项对应的成品路径
         * @return 路径；没有选中项时返回空
         */
        [[nodiscard]] std::optional<std::filesystem::path> selectedPath() const;

    signals:
        /**
         * @brief 要求把这条成品重新贴回屏幕
         * @param filePath 成品文件路径
         */
        void pinRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求对这条成品做一次文字识别
         * @param filePath 成品文件路径
         */
        void ocrRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求从历史清单里去掉这条（成品文件保留）
         * @param filePath 成品文件路径
         */
        void removeRequested(const std::filesystem::path &filePath);

        /**
         * @brief 请求在文件管理器里打开这条记录成品所在的那个目录
         * @param filePath 选中的那条记录，目录由它的父级给出
         */
        void openFolderRequested(const std::filesystem::path &filePath);

        /**
         * @brief 请求清空整份历史清单（不动成品文件）
         */
        void clearAllRequested();

        /**
         * @brief 一条要落进气泡的提示
         * @param message 中文正文
         * @param kind 语义种类
         */
        void messageForUser(const QString &message, Gui::ToastKind kind);

    private:
        /// 按当前搜索词与类别重填列表，不动 m_entries
        void refillList();

        /// 读缩略图；读不到就退回这一类的图标
        [[nodiscard]] QIcon iconFor(const HistoryEntry &entry) const;

        /// 删除选中项：先去掉记录，再刷新列表
        void removeSelected();

        HistoryStore          &m_store;            ///< 历史清单
        HistoryRetentionPolicy m_policy;           ///< 读清单时用的保留策略
        Gui::IconProvider      &m_icons;            ///< 图标供给器
        QVector<HistoryEntry>  m_entries;          ///< 本次读到的全量记录，时间倒序
        QLineEdit             *m_search = nullptr; ///< 搜索框
        QComboBox             *m_filter = nullptr; ///< 类别筛选下拉
        QListWidget           *m_list   = nullptr; ///< 缩略图列表
    };

} // namespace GyanisScribe::History
