/**
 * @file PpOcrPostprocess.h
 * @brief PP-OCR 检测框后处理与识别结果的 CTC 解码
 * @author Gyanis
 * @date 2026-11-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace GyanisScribe::Core
{

    /**
     * @brief 一个检测文本框
     */
    struct PpOcrTextBox
    {
        QRect rect;         ///< 外扩并夹回图像范围后的矩形
        float score = 0.0F; ///< 连通域内的概率均值
    };

    /**
     * @brief 识别网络的解码结果
     */
    struct PpOcrDecodedText
    {
        QString text;             ///< 合并重复并去掉 blank 后的文本
        double  confidence = 0.0; ///< 保留字符所在步的最大概率均值，没有字符时为 0
    };

    /**
     * @brief 从检测网络的概率图提取文本框
     * @param map 概率图，行优先，长度等于 width 乘 height
     * @param width 概率图宽
     * @param height 概率图高
     * @param threshold 二值化阈值，像素值达到它才算文字
     * @param minArea 连通域最小面积，像素数小于它的连通域丢弃
     * @param unclipRatio 外扩系数，按连通域短边乘该系数后取整，再与下限 2 取大
     * @return 按阅读顺序排列的文本框；空输入或参数非法时返回空列表
     */
    [[nodiscard]] QVector<PpOcrTextBox> extractTextBoxes(const float *map, int width, int height, float threshold = 0.3F, float minArea = 3.0F, float unclipRatio = 0.2F);

    /**
     * @brief 按 CTC 规则解码识别网络的输出
     * @param logits 每步各类的分数，行优先，长度等于 timeSteps 乘 classes
     * @param timeSteps 时间步数
     * @param classes 类别数，第 0 类是 blank
     * @param dictionary 字表，索引 i 落在 1 到字表长度之间时取第 i-1 项；索引等于字表长度加 1 时输出一个空格
     * @return 解码出的文本与置信度；空输入或参数非法时返回空文本
     */
    [[nodiscard]] PpOcrDecodedText decodeCtc(const float *logits, int timeSteps, int classes, const QStringList &dictionary);

} // namespace GyanisScribe::Core
