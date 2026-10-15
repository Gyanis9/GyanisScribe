/**
 * @file HistoryTypes.h
 * @brief 历史记录的值类型：一条成品是什么、属于哪一类
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QDateTime>
#include <QSize>
#include <QString>

#include <filesystem>
#include <string_view>

namespace GyanisScribe::History
{

    /**
     * @brief 历史成品的类别
     * @details 只列软件真的会产出的两类；被用例钉住的枚举新成员只能追加在末尾。
     */
    enum class HistoryKind
    {
        Screenshot, ///< 截图与标注成品（PNG）
        Recording,  ///< 录屏成品（MP4 或 GIF）
    };

    /**
     * @brief 类别的存储标记
     * @param kind 类别
     * @return 写进历史文件的短标记，区分大小写
     */
    [[nodiscard]] std::string_view historyKindToken(HistoryKind kind);

    /**
     * @brief 类别的中文显示名
     * @param kind 类别
     * @return 「截图」或「录屏」，列表与筛选下拉都用这一份
     */
    [[nodiscard]] QString historyKindLabel(HistoryKind kind);

    /**
     * @brief 把存储标记读回类别
     * @param token 历史文件里的短标记
     * @return 类别；标记不认识时返回错误并列出可用标记
     * @details 不认得就报错而不是回落成 Screenshot：历史文件是用户能手工改的，
     *          静默改写类别会让下一条「重新贴图」找错文件。
     */
    [[nodiscard]] Base::Expected<HistoryKind> parseHistoryKind(std::string_view token);

    /**
     * @brief 一条历史记录
     * @details 成品文件本身不进历史文件，只记路径、时间与尺寸：这样历史不会因为几百张图而膨胀，
     *          而「重新贴图」与「重新识别」需要的像素随时能从路径再读一次。
     */
    struct HistoryEntry
    {
        QDateTime             moment;   ///< 产出时间，列表按它倒序
        HistoryKind           kind{};   ///< 类别
        std::filesystem::path filePath; ///< 成品文件路径，同时是这条记录的身份
        QSize                 size;     ///< 成品像素尺寸；视频记的是画面尺寸
    };

} // namespace GyanisScribe::History
