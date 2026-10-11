/**
 * @file Paths.h
 * @brief 应用各用途目录的默认取值，全部经 QStandardPaths 解析
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QString>

#include <filesystem>
#include <string_view>

class QDateTime;

namespace GyanisScribe::Core
{

    /**
     * @brief 目录提供者
     * @details 只给默认值，不读设置：用户改过的截图/录屏目录由 Settings 提供，由调用方（App 层）在这里之外
     *          做替换。这样 Core 不依赖 Settings，容器内无 GUI 也能跑用例。
     * @note 全部方法都要在 QCoreApplication 存在之后调用，QStandardPaths 的取值依赖应用与组织名已设置。
     */
    class Paths
    {
    public:
        /**
         * @brief 应用可写数据根目录
         * @return 已存在或即将存在的目录路径
         */
        [[nodiscard]] static std::filesystem::path applicationDataDirectory();

        /**
         * @brief 可执行体所在目录
         * @return 安装/解压后的程序目录
         * @details 随包分发的第三方件（ffmpeg、识别语言包）都从这里往下找：软件要装完即用，
         *          不能要求用户再往系统里装一份或自己放文件。
         */
        [[nodiscard]] static std::filesystem::path applicationDirectory();

        /**
         * @brief 日志目录
         * @return 位于可写数据目录下的 logs
         */
        [[nodiscard]] static std::filesystem::path logDirectory();

        /**
         * @brief 缓存目录
         * @return 系统缓存位置下的应用子目录，历史缩略图放这里
         */
        [[nodiscard]] static std::filesystem::path cacheDirectory();

        /**
         * @brief 截图默认目录
         * @return 用户图片目录下的 GyanisScribe
         */
        [[nodiscard]] static std::filesystem::path defaultScreenshotDirectory();

        /**
         * @brief 录屏默认目录
         * @return 用户视频目录下的 GyanisScribe
         */
        [[nodiscard]] static std::filesystem::path defaultRecordingDirectory();

        /**
         * @brief 确保目录存在
         * @param directory 目标目录
         * @return 成功返回 void；失败返回原因，文案里带目录与系统错误
         */
        /**
         * @brief 路径换成能直接拼进界面文案的字符串
         * @param path 文件路径
         * @return UTF-16 的那串路径
         * @details 走 wstring 而不是 narrow string：Windows 上 narrow 按本地 ANSI 码页解释，
         *          中文用户名下的截图目录会被读成另一条不存在的路。
         */
        [[nodiscard]] static QString toDisplayText(const std::filesystem::path &path);

        /**
         * @brief 把 toDisplayText 的结果换回路径
         * @param text 界面里的那串路径
         * @return 文件路径
         */
        [[nodiscard]] static std::filesystem::path fromDisplayText(const QString &text);

        [[nodiscard]] static Expected<void> ensureDirectory(const std::filesystem::path &directory);

        /**
         * @brief 生成导出图像的文件名
         * @param moment 导出时刻
         * @param extension 扩展名，不带点，例如 png、mp4
         * @param pattern 文件名模板；留空表示用 kDefaultNameTemplate（与「目录留空表示默认目录」同一约定）
         * @return 形如 GyanisScribe_20261009_235959_123.png 的文件名
         * @details 命名规则的唯一出处：贴图保存、标注保存、识别导出与录屏成品共用同一个格式，
         *          几处各写一份就会在某一天变成同一目录里几套文件名。
         *          pattern 不合法（含路径分隔符、未知变量）时记一条警告并按默认档走：
         *          宁可写一个能用的名字，也不能把文件写到模板里那个分隔符指去的地方。
         */
        [[nodiscard]] static QString imageFileName(const QDateTime &moment, std::string_view extension, const QString &pattern = {});

        /// 成品文件名的默认模板：三格变量是日期、时刻与毫秒；毫秒那一格让同一秒里连拍不互相覆盖
        static constexpr std::string_view kDefaultNameTemplate = "GyanisScribe_%date%_%time%_%ms%";

        /// 默认模板的 QString 形态：配置键、界面与拼名那几处都要用它，少写几次转换
        [[nodiscard]] static QString defaultNameTemplate();

        /// 便携模式的标记文件名：程序目录下放一个同名空文件就切过去
        static constexpr std::string_view kPortableMarkerFileName = "portable.txt";

        /**
         * @brief 看某个目录里有没有便携标记，有就把配置落在它旁边
         * @param directory 待查目录（生产传程序目录，用例传临时目录）
         * @return 便携配置文件的完整路径；目录里没有标记时返回空串
         * @details 用「显式放一个标记文件」而不是「默认就近」：装到 Program Files 这类目录下时
         *          程序目录不可写，默认就近会直接坏掉，而绿色版用户放个标记是明确的选择。
         *          只有配置跟着走：日志、历史、缓存与成品目录仍在各自的位置（见 README）。
         */
        [[nodiscard]] static QString portableConfigFileIn(const QString &directory);

        /**
         * @brief 程序目录里的便携配置（没有标记时返回空串）
         * @return 便携配置文件路径，或空串表示走系统默认位置
         */
        [[nodiscard]] static QString portableConfigFile();

        /**
         * @brief 校验一份文件名模板
         * @param text 用户填的模板
         * @return 成功返回可用的模板（已去掉首尾空白）；不合法时返回带中文原因的 InvalidArgument
         * @details 只认三格变量：%date%、%time%、%ms%，其余按字面量写进名字。
         *          路径分隔符与 Windows 上的保留字符一律拒绝——放过去就是写到别的目录或写不出去；
         *          形如 %xxx% 的未知变量也拒绝：打错一个字不该悄悄写进文件名。
         */
        [[nodiscard]] static Expected<QString> parseNameTemplate(const QString &text);

        /**
         * @brief 按模板拼出成品名
         * @param moment 产出时刻
         * @param pattern 已校验过的模板
         * @return 不含扩展名的名字
         */
        [[nodiscard]] static QString formatProductName(const QDateTime &moment, const QString &pattern);

        /**
         * @brief 取一个不与已有文件相撞的路径
         * @param directory 目标目录
         * @param fileName 想用的文件名（含扩展名）
         * @return 目录下的路径；同名文件已存在时在扩展名前加 -2、-3……
         * @details 模板里去掉 %ms% 之后同一秒连拍会撞名，直接写就是把前一张悄悄盖掉。
         *          试到 -99 仍撞名就交回原路径：再往下试只是把名字拉长，不如让调用方按写失败处理。
         */
        [[nodiscard]] static std::filesystem::path uniqueFilePath(const std::filesystem::path &directory, const QString &fileName);
    };

} // namespace GyanisScribe::Core
