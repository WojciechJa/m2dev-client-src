#include "BinPacker.h"
#include <algorithm>
#include <limits>

BinPacker::BinPacker(uint32_t atlasWidth, uint32_t atlasHeight)
    : m_uAtlasWidth(atlasWidth)
    , m_uAtlasHeight(atlasHeight)
    , m_uUsedArea(0)
{
    // Initialize with one large free rectangle covering entire atlas
    m_vFreeRects.push_back(FreeRect(0, 0, atlasWidth, atlasHeight));
}

BinPacker::~BinPacker()
{
}

bool BinPacker::Pack(Rect& rect)
{
    // Find best fitting free rectangle
    int iBestIdx = FindBestFreeRect(rect.width, rect.height);
    if (iBestIdx < 0)
        return false;  // No space available

    const FreeRect& bestFree = m_vFreeRects[iBestIdx];

    // Place rectangle at top-left of free space
    rect.x = bestFree.x;
    rect.y = bestFree.y;

    // Split the free rectangle and update free list
    SplitFreeRect(bestFree, rect);

    // Remove the used free rectangle
    m_vFreeRects.erase(m_vFreeRects.begin() + iBestIdx);

    // Prune redundant free rectangles
    PruneFreeRects();

    // Update used area
    m_uUsedArea += rect.width * rect.height;

    return true;
}

uint32_t BinPacker::PackAll(std::vector<Rect>& rects)
{
    // Sort rectangles by area (largest first) for better packing
    std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) {
        uint32_t areaA = a.width * a.height;
        uint32_t areaB = b.width * b.height;
        if (areaA != areaB)
            return areaA > areaB;
        // Tie-breaker: prefer taller rectangles
        return a.height > b.height;
    });

    uint32_t uPackedCount = 0;
    for (auto& rect : rects)
    {
        if (Pack(rect))
            ++uPackedCount;
        else
            break;  // Stop at first failure
    }

    return uPackedCount;
}

float BinPacker::GetEfficiency() const
{
    uint32_t uTotalArea = m_uAtlasWidth * m_uAtlasHeight;
    if (uTotalArea == 0)
        return 0.0f;
    return static_cast<float>(m_uUsedArea) / static_cast<float>(uTotalArea);
}

int BinPacker::FindBestFreeRect(uint32_t width, uint32_t height) const
{
    int iBestIdx = -1;
    uint32_t uBestShortSideFit = std::numeric_limits<uint32_t>::max();
    uint32_t uBestLongSideFit = std::numeric_limits<uint32_t>::max();

    for (size_t i = 0; i < m_vFreeRects.size(); ++i)
    {
        const FreeRect& freeRect = m_vFreeRects[i];

        // Check if rect fits
        if (freeRect.width >= width && freeRect.height >= height)
        {
            uint32_t leftoverX = freeRect.width - width;
            uint32_t leftoverY = freeRect.height - height;
            uint32_t shortSideFit = std::min(leftoverX, leftoverY);
            uint32_t longSideFit = std::max(leftoverX, leftoverY);

            // Best Short Side Fit (BSSF) heuristic
            if (shortSideFit < uBestShortSideFit ||
                (shortSideFit == uBestShortSideFit && longSideFit < uBestLongSideFit))
            {
                iBestIdx = static_cast<int>(i);
                uBestShortSideFit = shortSideFit;
                uBestLongSideFit = longSideFit;
            }
        }
    }

    return iBestIdx;
}

void BinPacker::SplitFreeRect(const FreeRect& freeRect, const Rect& placedRect)
{
    // Generate new free rectangles from the split

    // Right side remainder
    if (placedRect.x + placedRect.width < freeRect.x + freeRect.width)
    {
        FreeRect rightRect;
        rightRect.x = placedRect.x + placedRect.width;
        rightRect.y = freeRect.y;
        rightRect.width = (freeRect.x + freeRect.width) - rightRect.x;
        rightRect.height = freeRect.height;
        m_vFreeRects.push_back(rightRect);
    }

    // Bottom side remainder
    if (placedRect.y + placedRect.height < freeRect.y + freeRect.height)
    {
        FreeRect bottomRect;
        bottomRect.x = freeRect.x;
        bottomRect.y = placedRect.y + placedRect.height;
        bottomRect.width = freeRect.width;
        bottomRect.height = (freeRect.y + freeRect.height) - bottomRect.y;
        m_vFreeRects.push_back(bottomRect);
    }
}

void BinPacker::PruneFreeRects()
{
    // Remove free rectangles that are fully contained within another free rectangle
    for (size_t i = 0; i < m_vFreeRects.size(); ++i)
    {
        for (size_t j = i + 1; j < m_vFreeRects.size(); )
        {
            if (IsContainedIn(m_vFreeRects[i], m_vFreeRects[j]))
            {
                // m_vFreeRects[i] is contained in m_vFreeRects[j], remove i
                m_vFreeRects.erase(m_vFreeRects.begin() + i);
                --i;  // Re-check this index
                break;
            }
            else if (IsContainedIn(m_vFreeRects[j], m_vFreeRects[i]))
            {
                // m_vFreeRects[j] is contained in m_vFreeRects[i], remove j
                m_vFreeRects.erase(m_vFreeRects.begin() + j);
            }
            else
            {
                ++j;
            }
        }
    }
}

bool BinPacker::IsContainedIn(const FreeRect& a, const FreeRect& b)
{
    return a.x >= b.x && a.y >= b.y &&
           (a.x + a.width) <= (b.x + b.width) &&
           (a.y + a.height) <= (b.y + b.height);
}
