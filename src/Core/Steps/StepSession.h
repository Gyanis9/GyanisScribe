/**
 * @file StepSession.h
 * @brief 一次步骤记录的步骤表、编辑操作与临时截图目录
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Steps/StepTypes.h"

#include <QDateTime>
#include <QImage>
#include <QString>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace GyanisScribe::Core
{
    /**
     * @brief 一次步骤记录的步骤表与临时截图目录
     * @details 所有方法参数里的 index 都是步骤数组下标，从 0 开始；RecordedStep::index
     *          是显示序号，从 1 开始，删除或调序后整体重排。两者不要混用。
     *
     *          整图写在临时目录的 step-001.png 这类文件名下，缩略图只留在内存。
     *          文件名写定后不再随调序改名，追加时改用下一个未被占用的编号，避免覆盖仍在使用的图片。
     *
     *          析构不删临时目录：停止录制后审核窗还要读图，由放弃、关闭审核窗或启动清理回收。
     */
    class StepSession
    {
    public:
        /**
         * @brief 建立一次空会话并创建临时目录
         * @param tempDirectory 本次会话的临时目录，通常在 Steps 下按时刻取名
         * @return 成功返回空载荷；路径为空或建目录失败时返回带路径的中文 Error
         * @details 先建目录，成功后才清空已有步骤并记下新目录；失败时步骤表与旧临时目录都保持原样。
         *          旧临时目录不在这里删除，交给 cleanup 或启动清理。
         */
        [[nodiscard]] Base::Expected<void> begin(const std::filesystem::path &tempDirectory);

        /**
         * @brief 追加一步并自动编号
         * @param step 待追加的步骤；说明为空时按 kind 生成自动说明
         * @return 成功返回空载荷；步骤已达上限时返回中文 Error
         * @details 说明会去掉控制字符、折叠连续空白，并截到 500 字符。
         */
        [[nodiscard]] Base::Expected<void> append(RecordedStep step);

        /**
         * @brief 把整屏截图写成 PNG 并生成缩略图
         * @param index 步骤数组下标，从 0 开始
         * @param image 整屏图，物理像素
         * @return 成功返回空载荷；下标越界、尚未 begin、空图或写盘失败时返回中文 Error
         * @details 失败原因同时写进该步骤的 captureError，供审核窗与导出显示占位。
         *          重贴成功且换了文件名时删掉这一步骤的旧图，避免旧 PNG 留在临时目录里成为孤儿。
         */
        [[nodiscard]] Base::Expected<void> attachImage(std::size_t index, const QImage &image);

        /**
         * @brief 删除一步并删掉它的整图文件
         * @param index 步骤数组下标，从 0 开始
         * @return 下标越界返回 false，否则 true
         * @details 整图文件已经不在时视为正常；其他删除失败只记 Warning 日志，步骤照常移除。
         */
        bool remove(std::size_t index);

        /**
         * @brief 把一步前移一位
         * @param index 步骤数组下标，从 0 开始
         * @return 下标越界或已在首位时返回 false
         */
        bool moveUp(std::size_t index);

        /**
         * @brief 把一步后移一位
         * @param index 步骤数组下标，从 0 开始
         * @return 下标越界或已在末位时返回 false
         */
        bool moveDown(std::size_t index);

        /**
         * @brief 改写一步的说明
         * @param index 步骤数组下标，从 0 开始
         * @param text 新说明；允许空串，空串按用户清空处理，不再回填自动说明
         * @return 成功返回空载荷；下标越界时返回中文 Error
         * @details 与 append 一样折叠空白并截到 500 字符。
         */
        [[nodiscard]] Base::Expected<void> setDescription(std::size_t index, const QString &text);

        /**
         * @brief 当前步骤数
         * @return 步骤数组的长度
         */
        [[nodiscard]] std::size_t count() const noexcept;

        /**
         * @brief 只读步骤表
         * @return 按显示顺序排列的步骤，下标从 0 开始
         */
        [[nodiscard]] const std::vector<RecordedStep> &steps() const noexcept;

        /**
         * @brief 本次会话的临时目录
         * @return begin 成功后的目录；尚未 begin 或 cleanup 之后为空路径
         */
        [[nodiscard]] const std::filesystem::path &tempDirectory() const noexcept;

        /**
         * @brief 删除整个临时目录并清空步骤
         * @details 删目录失败不抛出，残留目录由下次启动的清理回收。
         */
        void cleanup();

        /**
         * @brief 判断紧邻的一对点击是否应合并为同一步
         * @param previous 已提交的上一步
         * @param next 新到的点击
         * @return 两者都是点击、时间差落在 0 到 1.5 秒之间、位移不超过 4 像素、
         *         元素名与窗口标题都相同时为 true
         * @details 只读判定，不改动任何步骤；时间差为负（next 早于 previous）时不合并；
         *          元素名都为空也算相同，此时按坐标合并。
         */
        [[nodiscard]] bool shouldMerge(const RecordedStep &previous, const RecordedStep &next) const noexcept;

        /**
         * @brief 删除 root 下超过 24 小时的会话目录
         * @param root 会话目录的上一级，通常是 Steps
         * @param now 判定用的当前时刻；测试可注入固定时间
         * @details 只看 root 的直接子目录，文件与更新的目录都不碰；删除失败静默跳过。
         */
        static void cleanupOldSessions(const std::filesystem::path &root, const QDateTime &now);

    private:
        /// 按当前顺序重排显示序号
        void bumpIndices() noexcept;

        /**
         * @brief 判断某个图片路径是否已被其他步骤占用
         * @param path 候选整图路径
         * @param exceptIndex 允许占用的步骤下标，通常是正在贴图的那一步
         * @return 被其他步骤引用时为 true
         */
        [[nodiscard]] bool isImagePathInUse(const std::filesystem::path &path, std::size_t exceptIndex) const;

        std::vector<RecordedStep> m_steps;         ///< 按显示顺序保存的步骤
        std::filesystem::path     m_tempDirectory; ///< 本次会话的临时目录；空表示尚未 begin
    };

} // namespace GyanisScribe::Core
