with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Replace the SampleDungeonTexture function - find from the function start to the closing brace
old_func = '''uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v) {
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
        // Bottom 16 rows: triangle (LeftTriangle encoding)
        // Top 16 rows: rectangle (64x16)
        if (texY < 16) {
            // Triangle part - similar to LeftTriangle
            int row = 15 - texY; // Bottom-up
            int width = (row + 1) * 2; // 2, 4, 6, ..., 32
            int startX = (TEXTURE_SIZE - width) / 2;
            if (texX < startX || texX >= startX + width)
                return 0; // Transparent
            int srcX = texX - startX;
            int srcOffset = (row * (row + 1)) + srcX; // Simplified triangle indexing
            return frameData[srcOffset % (ReencodedTrapezoidFrameSize)];
        } else {
            // Rectangle part (top 16 rows)
            int rectY = texY - 16;
            int srcOffset = ReencodedTrapezoidFrameSize - (16 * TEXTURE_SIZE) + rectY * TEXTURE_SIZE + texX;
            return frameData[srcOffset];
        }
    }
    case TileType::RightTrapezoid: {
        // Mirror of LeftTrapezoid
        if (texY < 16) {
            int row = 15 - texY;
            int width = (row + 1) * 2;
            int startX = (TEXTURE_SIZE - width) / 2;
            if (texX < startX || texX >= startX + width)
                return 0;
            int srcX = texX - startX;
            int srcOffset = (row * (row + 1)) + srcX;
            return frameData[srcOffset % (ReencodedTrapezoidFrameSize)];
        } else {
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
    default:
        return 0;
    }
}'''

new_func = '''uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v, bool isWall = false) {
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
        // For now, return a color based on position
        return static_cast<uint8_t>(64 + (texX + texY) % 128);
    }
    default:
        return 0;
    }
}'''

import re
# Use a more specific pattern
content = open('Source/engine/render/fp_render.cpp', 'r').read()

# Find and replace the function - use a marker-based approach
# Find the function and replace up to the closing brace at the right level
pattern = r'uint8_t SampleDungeonTexture\(const uint8_t \*frameData, TileType tileType, float u, float v\) \{(.*?)\n\}'

# We'll do a more targeted replacement using line-based approach
with open('Source/engine/render/fp_render.cpp', 'r') as f:
    lines = f.readlines()

# Find the function start and end
start_line = -1
end_line = -1
brace_count = 0
in_function = False

for i, line in enumerate(lines):
    if 'uint8_t SampleDungeonTexture(' in line and 'bool isWall' not in line:
        start_line = i
        in_function = True
        brace_count = 0
    if in_function:
        brace_count += line.count('{')
        brace_count -= line.count('}')
        if brace_count == 0 and in_function:
            end_line = i
            break

if start_line != -1 and end_line != -1:
    print(f"Found function at lines {start_line}-{end_line}")
    # Replace lines[start_line:end_line+1] with new function
    new_func_lines = '''uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v, bool isWall = false) {
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
        // For now, return a color based on position
        return static_cast<uint8_t>(64 + (texX + texY) % 128);
    }
    default:
        return 0;
    }
}'''.splitlines(keepends=True)

    # Replace
    new_lines = lines[:start_line] + new_func_lines + lines[end_line+1:]
    with open('Source/engine/render/fp_render.cpp', 'w') as f:
        f.writelines(new_lines)
    print("Replacement done!")
else:
    print("Function not found!")
