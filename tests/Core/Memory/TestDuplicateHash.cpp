// 钉住 64 位 dHash 的稳定性、Hamming 距离边界与重复图分组的组间、组内顺序

#include "Core/Memory/DuplicateHash.h"
#include "Core/Memory/MemoryTypes.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

using GyanisScribe::Core::computeDHash;
using GyanisScribe::Core::groupDuplicates;
using GyanisScribe::Core::hammingDistance;
using GyanisScribe::Core::MemoryResult;

namespace
{
    QImage stripedImage()
    {
        QImage image{18, 16, QImage::Format_Grayscale8};
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                image.setPixel(x, y, (x / 2 + y / 2) % 2 == 0 ? 0 : 255);
            }
        }
        return image;
    }

    MemoryResult resultWithHash(const QString &hash, const int minute)
    {
        MemoryResult result;
        result.id         = minute;
        result.dhash      = hash;
        result.capturedAt = QDateTime::fromSecsSinceEpoch(minute * 60);
        return result;
    }
} // namespace

TEST(DuplicateHash, SameImageHasSameHashAndDifferentImageDoesNot)
{
    const QImage first     = stripedImage();
    const QImage second    = first;
    QImage       different = first;
    different.invertPixels();

    const QString hash = computeDHash(first);
    EXPECT_EQ(hash.size(), 16);
    EXPECT_EQ(hash, computeDHash(second));
    EXPECT_NE(hash, computeDHash(different));
    EXPECT_TRUE(computeDHash(QImage{}).isEmpty());
}

TEST(DuplicateHash, GroupsExactAndNearHashesButNotDistantOnes)
{
    const QString base = QStringLiteral("0000000000000000");
    const QString near = QStringLiteral("0000000000000001");
    const QString far  = QStringLiteral("ffffffffffffffff");
    EXPECT_EQ(hammingDistance(base, near), 1);
    EXPECT_EQ(hammingDistance(base, far), 64);
    EXPECT_EQ(hammingDistance(QString{}, base), 64);

    const auto groups = groupDuplicates({resultWithHash(base, 3), resultWithHash(near, 2), resultWithHash(far, 1)});
    ASSERT_EQ(groups.size(), 2U);
    EXPECT_EQ(groups.at(0).size(), 2U);
    EXPECT_EQ(groups.at(1).size(), 1U);
}
