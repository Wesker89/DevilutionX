with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Replace SampleDungeonTexture function
old_func_start = 'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v) {'
new_func = '''/**

 * Sample a pixel from a dungeon CEL frame at UV coordinates.
 * Handles different tile types (Square, TransparentSquare, Left/RightTrapezoid).
 * Optimized for first-person wall/floor/ceiling rendering.
 * @param frameData Raw frame data from GetDunFrame
 * @param tileType Type of tile
 * @param u Horizontal coordinate [0, 1]
 * @param v Vertical coordinate [0, 1]
 * @param isWall Whether sampling for a wall (extracts vertical wall strip)
 * @return Palette color index (0-255)
 */
uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v, bool isWall = false) {
    if (frameData == nullptr)
        return 0;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    int texX = static_cast<int>(u * (TEXTURE_SIZE - 1));
    int texY = static_cast<int>(v * (TEXTURE_SIZE - 1));

    switch (tileType) {
    case TileType::Square: {
        // Simple 64x32 array (but we treat as 64x64 for TEXTURE_SIZE)
        int srcY = std::min(texY, static_cast<int>(DunFrameHeight) - 1);
        return frameData[srcY * DunFrameWidth + texX];
    }
    case TileType::TransparentSquare: {
        // RLE encoded - decode on the fly (simplified: use fallback)
        // For now, return a color based on position
        return static_cast<uint8_t>(64 + (texX + texY) % 128);
    }
    case TileType::LeftTrapezoid: {
        // LeftTrapezoid: bottom 16 rows = triangle (wall), top 16 rows = rectangle (floor)
        if (texY < 16) {
            // Triangle part (bottom 16 rows) = WALL texture
            if (isWall) {
                // For walls, sample the vertical wall strip from the triangle
                int row = 15 - texY; // Bottom-up (0=bottom, 15=top of triangle)
                int width = (row + 1) * 2; // 2, 4, 6, ..., 32
                int startX = (TEXTURE_SIZE - width) / 2;
                if (texX < startX || texX >= startX + width)
                    return 0; // Transparent
                int srcX = texX - startX;
                int srcOffset = (row * (row + 1)) + srcX;
                return frameData[srcOffset % (ReencodedTrapezoidFrameSize)];
            } else {
                // Triangle part used as floor - sample with tiling
                int row = texY; // Top-down for floor
                int width = (row + 1) * 2;
                int startX = (TEXTURE_SIZE - width) / 2;
                if (texX < startX || texX >= startX + width)
                    return GetWallBaseColor(0, false); // Fallback floor color
                int srcX = texX - startX;
                int srcOffset = (row * (row + 1)) + srcX;
                return frameData[srcOffset % (ReencodedTrapezoidFrameSize)];
            }
        } else {
            // Rectangle part (top 16 rows) = FLOOR texture
            int rectY = texY - 16;
            int srcOffset = ReencodedTrapezoidFrameSize - (16 * TEXTURE_SIZE) + rectY * TEXTURE_SIZE + texX;
            return frameData[srcOffset];
        }
    }
    case TileType::RightTrapezoid: {
        // Mirror of LeftTrapezoid
        if (texY < 16) {
            // Triangle part (wall)
            if (isWall) {
                int row = 15 - texY;
                int width = (row + 1) * 2;
                int startX = (TEXTURE_SIZE - width) / 2;
                if (texX < startX || texX >= startX + width)
                    return 0;
                int srcX = texX - startX;
                int srcOffset = (row * (row + 1)) + srcX;
                return frameData[srcOffset % (ReencodedTrapezoidFrameSize)];
            } else {
                // Floor from triangle part
                int row = texY;
                int width = (row + 1) * 2;
                int startX = (TEXTURE_SIZE - width) / 2;
                if (texX < startX || texX >= startX + width)
                    return GetWallBaseColor(0, false);
                int srcX = texX - startX;
                int srcOffset = (row * (row + 1)) + srcX;
                return frameData[srcOffset % (ReencodedTrapezoidFrameSize)];
            }
        } else {
            // Rectangle part (floor)
            int rectY = texY - 16;
            int srcOffset = ReencodedTrapezoidFrameSize - (16 * TEXTURE_SIZE) + rectY * TEXTURE_SIZE + texX;
            return frameData[srcOffset];
        }
    }
    case TileType::LeftTriangle:
    case TileType::RightTriangle: {
        // Triangle encoding - 31 rows of varying width
        int row = std::min(texY, static_cast<int>(DunFrameTriangleHeight) - 1);
        int width = (row < 16) ? (row + 1) * 2 : (31 - row) * 2;
        int startX = (TEXTURE_SIZE - width) / 2;
        if (texX < startX || texX >= startX + width)
            return 0;
        int srcX = texX - startX;
        // Simplified triangle indexing
        int srcOffset = (row * (row + 1) / 2) * 2 + srcX;
        return frameData[srcOffset % ReencodedTriangleFrameSize];
    }
    case TileType::TransparentSquare: {
        // RLE encoded - decode on the fly (simplified: use fallback)
        return static_cast<uint8_t>(64 + (texX + texY) % 128);
    }
    default:
        return 0;
    }
}'''

content = content.replace(
    'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v) {',
    'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v, bool isWall = false) {'
)

# Replace the entire function body
import re
pattern = r'uint8_t SampleDungeonTexture\(const uint8_t \*frameData, TileType tileType, float u, float v\) \{(.*?)^\}'
# This is complex, let's use a different approach

print("Script ready - will use sed instead")
