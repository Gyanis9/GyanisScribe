/**
 * @file PinManager.h
 * @brief 多张贴图的管理器：创建、关闭、全部关闭与布局存取
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Pin/IPinWindow.h"
#include "Pin/PinLayout.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QVector>

#include <memory>
#include <vector>

class QMimeData;

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Pin
{
    class PinWindow;

    /**
     * @brief 贴图管理器
     * @details 窗口由本管理器创建并持有；用户在窗口上按 Esc 或菜单里选关闭时，
     *          窗口发 closedByUser，管理器把自己记的那一份摘掉——两边都只有一条摘除路径。
     *          贴图按分组组织：桌上只放活动组的那几张，切组就是换一批（分组与活动组都进布局记录，
     *          重启后回到同一组）。哪张在哪一组记在贴图自己身上，管理器只按它算显隐。
     * @note 图片内容不进布局文件：恢复贴图要有图，而图从哪来（历史目录、剪贴板）是界面层的事，
     *       布局只管「摆在哪、多大、多透、哪一组」。
     */
    class PinManager : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 绑定配置与平台集成
         * @param settings 布局的读写处，生命周期必须长于本管理器
         * @param integration 平台能力来源，透传给每张贴图，生命周期必须长于本管理器
         * @param parent QObject 父对象，默认无
         */
        PinManager(Settings::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 析构：关掉还在的贴图窗口
         */
        ~PinManager() override;

        PinManager(const PinManager &)            = delete;
        PinManager &operator=(const PinManager &) = delete;

        /**
         * @brief 贴出一张图片
         * @param image 图片（物理像素）
         * @param globalTopLeft 窗口左上角的全局逻辑坐标
         * @param animatedSource 动图来源文件；空表示这张是静图（剪贴板与标注贴出来的都在内存里）
         * @return 新建的贴图窗口引用，所有权仍在管理器手里
         */
        IPinWindow &addPin(const QImage &image, const QPoint &globalTopLeft, const QString &animatedSource = {});

        /**
         * @brief 当前贴图张数
         * @return 张数
         */
        [[nodiscard]] int count() const noexcept;

        /**
         * @brief 指定位置的那张贴图
         * @param index 索引
         * @return 窗口引用；越界时返回空指针，调用方应先按 count() 判过界
         */
        [[nodiscard]] IPinWindow *pinAt(int index) const;

        /**
         * @brief 把当前布局写进配置，并把每张贴图的像素落到缓存目录
         * @return 成功返回空载荷；某一张写不下去时整批不存，返回带路径的原因
         * @details 布局里只有几何摆不回像素：保存那一刻给每张在
         *          Core::Paths::cacheDirectory()/Pins 下写一份 PNG，路径记进布局。
         */
        [[nodiscard]] Core::Expected<void> saveLayout();

        /**
         * @brief 按配置里那份布局把贴图摆回来
         * @return 成功返回真的摆出来的张数；布局本身读不出时返回 InvalidArgument 并点名坏在哪一项
         * @details 布局形状坏 → 整次失败（少摆一张会让用户以为那张被删了）；
         *          单个图片文件读不出 → 只跳过那一张并记一条日志（图是用户自己删的，
         *          让剩下的继续摆回来比整次放弃更接近他留下的桌面）。
         */
        [[nodiscard]] Core::Expected<int> restoreLayout();

        /**
         * @brief 关闭全部贴图
         */
        void closeAll();

        /**
         * @brief 让所有正在鼠标穿透的贴图重新吃鼠标
         * @return 恢复的张数；没有一张在穿透时返回 0
         * @details 穿透中的贴图收不到右键，也就关不掉自己那一档——这一条是那个死局的出口，
         *          放在托盘而不是贴在图上。
         */
        int clearMouseThrough();

        /**
         * @brief 桌面上还看得见几张贴图
         * @return 没被收起的张数；一张也没有时返回 0
         */
        [[nodiscard]] int visiblePinCount() const;

        /**
         * @brief 把全部贴图一起收起或摆回
         * @param concealed 收起还是摆回
         * @return 这次真的改变了状态的张数（本来就是那一档的不计）
         * @details 收起不等于关掉：图还在管理器手里，摆位与缩放都不动，只是不在屏上。
         *          收着的时候用户点不到它，也就没有「自己关不掉自己」那条死路——
         *          摆回这一格由托盘与热键给，不贴在图上。
         * @note 这一档只盖住当前分组：切到别的组时那一组的贴图照常露脸，
         *       全局收起的意思从「桌上什么都别留」变成了「这一组的先让开」
         */
        int setAllPinsConcealed(bool concealed);

        /**
         * @brief 现在显示的是第几组
         * @return 分组号，从 1 起
         */
        [[nodiscard]] int activeGroup() const noexcept;

        /**
         * @brief 现有分组号
         * @return 升序去重的分组号；一张贴图都没有时为空
         * @details 分组由贴图带着：一组里最后一张被关掉，这一组就不在了（不留空壳组，
         *          否则切换会在一个空组上停一站，看上去像贴图全丢了）
         */
        [[nodiscard]] QVector<int> groupsInUse() const;

        /**
         * @brief 切到下一组
         * @return 成功返回新的分组号；只有一组时返回 InvalidArgument 并说清怎么多出一组
         * @details 切换即重算显隐：上一组的收起来，这一组的摆出来。按「现有分组」的升序走一圈，
         *          空组不留停站。
         */
        [[nodiscard]] Core::Expected<int> switchToNextGroup();

        /**
         * @brief 把某张贴图移到别的分组
         * @param index 贴图索引，按创建顺序
         * @param group 目标分组号，从 1 起；比现有最大组号大的取值就是新建那一组
         * @return 成功返回空载荷；索引越界返回 InvalidArgument
         * @details 移出当前组的那张当场收起来——换组放着本来就是「暂时腾地方」的手段；
         *          要看它得切到那一组（托盘或热键）。
         */
        [[nodiscard]] Core::Expected<void> movePinToGroup(int index, int group);

        /**
         * @brief 把某张贴图的内容换成给定的那份像素
         * @param index 贴图在册表里的序号
         * @param image 新内容，物理像素
         * @return 成功返回空值
         * @return 失败带中文原因：贴图不在册，或那是一张动图（换上去的静态像素会被下一帧盖掉）
         * @details 摆位、缩放与角度都不动：这一条换的是「这张图」，不是「这张贴图摆在哪儿」。
         *          像素从剪贴板来，所以内容转换归界面层，这里只管在册与动图两道拒绝。
         */
        [[nodiscard]] Core::Expected<void> replacePinContent(int index, const QImage &image);

        /// 选中集里有几张（0 表示一张都没选）
        [[nodiscard]] int selectedPinCount() const noexcept;

        /**
         * @brief 把这一张切进/切出选中集（Ctrl+点选那一路）
         * @param pin 被点的那一张；切完把「一共几张」推给每一张（窗口不知道兄弟，选中集是这一层的账）
         */
        void togglePinSelection(PinWindow &pin);

        /**
         * @brief 把桌上看得见的贴图全部选进多选集（`Ctrl+A` 的落点）
         * @details 收着的与在别的组的那几张一律不选：整组动作落在看不见的东西上，比没生效更难解释。
         */
        void selectAllVisiblePins();

        /// 清空选中集（贴图自己的「取消选择」、切组与整桌收起时都走这里）
        void clearPinSelection();

        /// 这一张此刻是不是「只看这一张」的主角
        [[nodiscard]] bool pinIsSolo(const PinWindow &pin) const noexcept;

        /**
         * @brief 开关「只显示这一张」：开着时其余的全收起来，关掉恢复按组的那套规则
         * @param pin 主角那一张
         * @param solo true 表示只显示它
         * @details 新增贴图、切组、整桌收起与主角自己被关掉都会自动关掉这一档——
         *          那些时刻「只看这一张」已经指不动屏上的东西了
         */
        void setPinSolo(PinWindow &pin, bool solo);

    signals:
        /**
         * @brief 贴图张数变了
         * @param count 新的张数
         */
        void pinCountChanged(int count);

        /**
         * @brief 某张贴图上的「识别文字」被点了
         * @param image 那张贴图当前的图片
         * @details 管理器只转发，不自己识别：识别要用哪个后端、结果摆在哪个窗口，都是界面层的事，
         *          而贴图窗口归管理器持有，外面拿不到——所以出口收在这一层，一处接线管所有张。
         */
        void pinOcrRequested(const QImage &image);

        /**
         * @brief 某张贴图被移到了别的分组
         * @param group 它现在所在的分组号
         * @details 管理器只报事实：要不要提示用户「它被收起来了、切到那一组才看得见」是界面层的分寸
         */
        void pinMovedToGroup(int group);

        /**
         * @brief 某张贴图上取到了颜色
         * @param color 那一像素的颜色
         * @details 与 pinOcrRequested 同一条形状：贴图在外头拿不到，出口不收在这一层就没人能替它复制
         */
        void pinColorPicked(const QColor &color);

        /**
         * @brief 某张贴图在标注时要求把成品另贴一张
         * @param image 烘焙好的成品
         * @param globalTopLeft 新贴图左上角的全局坐标，按源贴图附近算好
         * @details 与 pinOcrRequested 同一条形状：只有这个出口能让界面层拿到「贴一张新的」这件事
         */
        void pinAnotherRequested(const QImage &image, const QPoint &globalTopLeft);

        /**
         * @brief 有东西被拖到某张贴图上（盘上的文件、链接或文本）
         * @param data 拖放载荷，只在本次事件处理期间有效
         * @param globalTopLeft 落点的全局坐标
         * @details 与 pinOcrRequested 同一条形状：贴图在外头拿不到，出口不收在这一层
         *          就没人能替它把载荷渲染成新贴图
         */
        void pinDropRequested(const QMimeData *data, const QPoint &globalTopLeft);

        /**
         * @brief 某张贴图要求用剪贴板里的内容替换自己（`Ctrl+V`）
         * @param index 发起那一张在册表里的序号，界面层据此找回是谁按的
         * @details 与 pinOcrRequested 同一条形状：剪贴板在这一层之外，出口只报「第几张按了替换键」，
         *          取什么内容、换成什么样都由界面层判。
         */
        void pinClipboardReplaceRequested(int index);

    private:
        /// 把某个已自行关闭的窗口从登记里摘掉
        void forget(PinWindow *window);

        /**
         * @brief 一次动作作用到哪些贴图上
         * @param source 发起动作的那一张
         * @return 这张在选中集里且选中不止一张时是整组，否则只有它自己
         */
        [[nodiscard]] std::vector<PinWindow *> pinsAffectedBy(PinWindow &source) const;

        /// 选中集变了之后把「一共几张」推给每一张
        void publishSelection();

        /// Solo 变了之后把「自己是不是主角」推给每一张（菜单项的勾选态照它走）
        void publishSolo();

        /// 活动组已经不在册（最后一张被关掉、或布局里换过组）时回落到第一组
        void normalizeActiveGroup();

        /**
         * @brief 按「活动组 + 全局收起」重算每张的显隐，并把分组清单推给每张
         * @return 这次真的改变了状态的张数
         * @details 显隐只有一个出口：贴图自己的 setConcealed 是唯一动作，
         *          两个来源（在哪一组、全局收起）合并成一条判据，才不会出现两处各写一半的隐身状态
         */
        int applyGroupVisibility();

        Settings::ISettingsStore               &m_settings;                      ///< 布局读写处
        Platform::IPlatformIntegration         &m_integration;                   ///< 平台能力来源
        std::vector<std::unique_ptr<PinWindow>> m_pins;                          ///< 在册贴图，按创建顺序
        int                                     m_activeGroup  = kFirstPinGroup; ///< 当前显示的分组号
        bool                                    m_allConcealed = false;          ///< 全局收起是不是开着
        PinWindow                              *m_solo         = nullptr;        ///< 「只看这一张」的主角；空表示没开
    };

} // namespace GyanisScribe::Pin
