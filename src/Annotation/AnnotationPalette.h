/**
 * @file AnnotationPalette.h
 * @brief 标注色板：可自定义的那一排颜色，读写、重排与分享文件的换算
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QColor>
#include <QString>

#include <cstddef>
#include <vector>

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Annotation
{

    /// 色板上限：再多就挤满工具栏那一排，导入超出这一档会被拒而不是悄悄截断
    inline constexpr std::size_t kPaletteCapacity = 16;

    /**
     * @brief 内置那八格（没自定义时的默认档）
     * @return 与 `Ui::kAnnotationPalette` 同序的颜色
     * @details 出处只有那一处：这一格是把常量表搬成 vector，不在界面上再抄一份颜色值。
     */
    [[nodiscard]] std::vector<QColor> defaultAnnotationPalette();

    /**
     * @brief 现在该摆哪几格
     * @param settings 配置存储
     * @return 配置里那份；这一格没填或全为空白时回落到内置那八格
     * @details 存的形状坏掉时不替用户猜一档：那会把「我存的色板呢」变成「程序给我换了八格别的」。
     *          读端与写端都在这一层，工具栏与设置页不发第二套解析。
     */
    [[nodiscard]] Base::Expected<std::vector<QColor>> annotationPalette(const Settings::ISettingsStore &settings);

    /**
     * @brief 把色板换成配置里能存的那个列表
     * @param palette 目标色板
     * @return 每格一个 #RRGGBB
     */
    [[nodiscard]] QStringList paletteToTokens(const std::vector<QColor> &palette);

    /**
     * @brief 把列表读成色板
     * @param tokens 每格一个 #RRGGBB
     * @return 色板；任一格写法不对、或格数超过上限时返回中文原因
     */
    [[nodiscard]] Base::Expected<std::vector<QColor>> paletteFromTokens(const QStringList &tokens);

    /**
     * @brief 拖一格的落点：把第 from 格挪到第 to 格那儿
     * @param palette 原色板
     * @param from 拖走的那一格下标
     * @param to 落点那一格下标（以原表为准，不是挪走之后的表）
     * @return 新色板；任一下标越界时原样交回（不静默改顺序）
     * @details 「挪到那一格那儿」而不是「跟那一格互换」：拖动要的是把这一块插到那一块前面，
     *          互换在拖过半排时会把用户的意图反着算。
     */
    [[nodiscard]] std::vector<QColor> movePaletteColor(std::vector<QColor> palette, std::size_t from, std::size_t to);

    /**
     * @brief 把一格颜色加进色板末尾
     * @param palette 原色板
     * @param color 要加的那一格
     * @return 新色板；表里已有同一格颜色、或已到上限、或颜色无效时原样交回
     * @details 去重按 #RRGGBB 的实际值比：同名不同值的两格在色板上看不出来差别，只是占地方。
     */
    [[nodiscard]] std::vector<QColor> addPaletteColor(std::vector<QColor> palette, const QColor &color);

    /**
     * @brief 从色板里去掉一格
     * @param palette 原色板
     * @param index 哪一格
     * @return 新色板；下标越界时原样交回
     */
    [[nodiscard]] std::vector<QColor> removePaletteColor(std::vector<QColor> palette, std::size_t index);

    /**
     * @brief 分享出去的那份文件的正文
     * @param palette 色板
     * @return 一行一格的 #RRGGBB，末尾带一个换行
     * @details 做成「一行一个」而不是 JSON：这一份要能手改、能从记事本粘进来，也能直接贴进工单里。
     */
    [[nodiscard]] QString paletteFileText(const std::vector<QColor> &palette);

    /**
     * @brief 把一份文件读成色板
     * @param text 文件正文
     * @return 色板；有空文件、写法不对或超出上限时返回中文原因
     * @details 认换行与逗号两种分隔（手写的清单与从别处粘的都可能），空行跳过但不算「一格内容都没有」；
     *          读回来的是整份替换，不是往现有色板后面追加——追加会让两次导入变成两份重复。
     */
    [[nodiscard]] Base::Expected<std::vector<QColor>> parsePaletteFileText(const QString &text);

} // namespace GyanisScribe::Annotation
