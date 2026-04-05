#pragma once

#include <vector>
#include <cstdint>

// Rectangle bin packing using MaxRects algorithm
// Efficient texture atlas packing with minimal wasted space
class BinPacker
{
public:
    struct Rect
    {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        void* pUserData;  // Custom data (e.g., texture filename)

        Rect() : x(0), y(0), width(0), height(0), pUserData(nullptr) {}
        Rect(uint32_t w, uint32_t h, void* pData = nullptr)
            : x(0), y(0), width(w), height(h), pUserData(pData) {}
    };

    BinPacker(uint32_t atlasWidth, uint32_t atlasHeight);
    ~BinPacker();

    // Pack a single rectangle into the atlas
    // Returns true if successfully packed, false if no space
    bool Pack(Rect& rect);

    // Pack multiple rectangles (sorts by size for better packing)
    // Returns number of successfully packed rectangles
    uint32_t PackAll(std::vector<Rect>& rects);

    // Get current packing efficiency (0.0 - 1.0)
    float GetEfficiency() const;

    // Get total used area
    uint32_t GetUsedArea() const { return m_uUsedArea; }

    // Get atlas dimensions
    uint32_t GetWidth() const { return m_uAtlasWidth; }
    uint32_t GetHeight() const { return m_uAtlasHeight; }

private:
    struct FreeRect
    {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;

        FreeRect() : x(0), y(0), width(0), height(0) {}
        FreeRect(uint32_t _x, uint32_t _y, uint32_t w, uint32_t h)
            : x(_x), y(_y), width(w), height(h) {}
    };

    uint32_t m_uAtlasWidth;
    uint32_t m_uAtlasHeight;
    uint32_t m_uUsedArea;
    std::vector<FreeRect> m_vFreeRects;

    // Find best free rectangle for given size
    int FindBestFreeRect(uint32_t width, uint32_t height) const;

    // Split free rectangle after placing a rect
    void SplitFreeRect(const FreeRect& freeRect, const Rect& placedRect);

    // Remove redundant free rectangles
    void PruneFreeRects();

    // Check if rect A is fully contained in rect B
    static bool IsContainedIn(const FreeRect& a, const FreeRect& b);
};
