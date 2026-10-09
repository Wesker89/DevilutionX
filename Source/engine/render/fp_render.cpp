/**
 * @file fp_render.cpp
 *
 * Implementation of first-person rendering mode using raycasting.
 * Renders the dungeon from the existing tile data using a 2.5D raycasting approach.
 */
#include "engine/render/fp_render.hpp"

#include "utils/sdl_compat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <vector>

#include "engine/point.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/direction.hpp"
#include "levels/dun_tile.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "levels/trigs.h"
#include "lighting.h"
#include "missiles.h"
#include "msg.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "player.h"
#include "quests.h"
#include "controls/plrctrls.h"
#include "utils/log.hpp"

namespace devilution {

// External globals from plrctrls.cpp and other files
extern int pcurstrig;
extern Missile *pcursmissile;
extern quest_id pcursquest;
extern int8_t pcursitem;
extern int pcursmonst;
extern Point cursPosition;
extern Object *ObjectUnderCursor;
extern const Player *PlayerUnderCursor;

namespace {

// Camera state
struct Camera {
    float x = 0.0f;      // World X position (tile units)
    float y = 0.0f;      // World Y position (tile units)
    float yaw = 0.0f;    // Horizontal rotation (radians)
    float pitch = 0.0f;  // Vertical rotation (radians)
    bool mouseLookEnabled = true; // FPS-style always-on mouselook
};

Camera g_camera;
bool g_cameraInitialized = false;

// Developer-only geometry diagnostic: flat untextured materials.
// Off by default; toggled with F4. Floors/walls/doors each get a
// contrasting flat color so layout problems show without texture noise.
bool g_geoDebug = false;

// FPS mouse look state
bool g_relativeMouseMode = false;

// Per-frame texture diagnostics: textured vs. fallback pixels.
// Reported periodically via LogInfo and the on-screen debug line so a
// missing-asset problem can be told apart from a mapping problem.
uint64_t g_diagWallTex = 0;
uint64_t g_diagWallFallback = 0;
uint64_t g_diagFloorTex = 0;
uint64_t g_diagFloorFallback = 0;
// Most recent material selections (tile coords + asset frame), so the log
// shows exactly which original graphics are mapped to visible surfaces.
Point g_lastWallTile { -1, -1 };
int g_lastWallFrame = 0;
int g_lastWallPiece = 0;
Point g_lastFloorTile { -1, -1 };
int g_lastFloorFrame = 0;

// Rendering constants
constexpr float FOV = 90.0f * M_PI / 180.0f;  // 90 degree field of view
constexpr float NEAR_PLANE = 0.05f;
constexpr float FAR_PLANE = 30.0f;
constexpr int MAX_RAY_DISTANCE = 50; // tiles

// Dungeon dimensions
constexpr int DUNGEON_WIDTH = MAXDUNX;
constexpr int DUNGEON_HEIGHT = MAXDUNY;

// Wall height in tile units
constexpr float WALL_HEIGHT = 2.5f;
constexpr float CEILING_HEIGHT = WALL_HEIGHT;

// Eye height above the floor in tile units. The floor caster assumes
// this (rowDistance = EYE_HEIGHT * screenHeight / p), so the wall
// framing below must use the same value or floors and walls separate.
constexpr float EYE_HEIGHT = 0.5f;

// Texture mapping (dungeon frames are DunFrameWidth wide)
constexpr int TEXTURE_SIZE = DunFrameWidth;

/**
 * Row width of a reencoded triangle frame row, indexed bottom-up (0 = bottom).
 * Rows 0..15 grow 2,4,...,32; rows 16..30 shrink 30,28,...,2.
 */
DVL_ALWAYS_INLINE int TriangleRowWidth(int row)
{
	return row < 16 ? 2 * (row + 1) : 2 * (31 - row);
}

/**
 * Byte offset of a reencoded triangle frame row, indexed bottom-up.
 * Lower rows: 0,2,6,12,... (triangular numbers * 2); upper rows follow at offset 272.
 */
DVL_ALWAYS_INLINE int TriangleRowOffset(int row)
{
	if (row < 16)
		return row * (row + 1);
	const int k = row - 16;
	return 272 + k * (31 - k);
}

/**
 * Sample a pixel from a dungeon CEL frame at UV coordinates.
 * Frame geometry follows dun_tile.hpp / dun_render.cpp:
 * - Square: flat 32x32 array, row 0 at the bottom.
 * - Left/RightTriangle: 31 rows bottom-up, widths 2..32..2 (512 bytes total).
 * - Left/RightTrapezoid: triangle rows 0..15 then a flat 32x16 rectangle (784 bytes).
 * Coordinates outside a triangle row are clamped into the row so sampling
 * never reads out of bounds and never leaves holes.
 * @param frameData Raw frame data from GetDunFrame
 * @param tileType Type of tile
 * @param u Horizontal coordinate [0, 1]
 * @param v Vertical coordinate [0, 1] (0 = bottom of the frame)
 * @return Palette color index (0-255)
 */
uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v) {
    if (frameData == nullptr)
        return 0;

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    switch (tileType) {
    case TileType::Square: {
        const int texX = std::min(static_cast<int>(u * DunFrameWidth), static_cast<int>(DunFrameWidth) - 1);
        const int texY = std::min(static_cast<int>(v * DunFrameHeight), static_cast<int>(DunFrameHeight) - 1);
        return frameData[texY * DunFrameWidth + texX];
    }
    case TileType::TransparentSquare: {
        // RLE encoded; no direct sampling. Caller falls back to a base color.
        return 0;
    }
    case TileType::LeftTrapezoid:
    case TileType::RightTrapezoid: {
        const int texY = std::min(static_cast<int>(v * DunFrameHeight), static_cast<int>(DunFrameHeight) - 1);
        if (texY < 16) {
            // Triangle part: reuses lower triangle rows 0..15.
            // Rows are right-aligned by the renderer (RenderTriangleLower
            // starts at the right edge and extends left), so srcX=0 is the
            // east end of the row; mirror u for left-pointing frames.
            const bool mirror = (tileType == TileType::LeftTrapezoid);
            const int width = TriangleRowWidth(texY);
            const int rawX = std::min(static_cast<int>(u * width), width - 1);
            const int texX = mirror ? (width - 1 - rawX) : rawX;
            return frameData[TriangleRowOffset(texY) + texX];
        }
        // Rectangle part: flat 32x16 block after the 272 triangle bytes.
        const int texX = std::min(static_cast<int>(u * DunFrameWidth), static_cast<int>(DunFrameWidth) - 1);
        return frameData[272 + (texY - 16) * DunFrameWidth + texX];
    }
    case TileType::LeftTriangle:
    case TileType::RightTriangle: {
        const int row = std::min(static_cast<int>(v * DunFrameTriangleHeight), static_cast<int>(DunFrameTriangleHeight) - 1);
        const int width = TriangleRowWidth(row);
        const int rawX = std::min(static_cast<int>(u * width), width - 1);
        // Left triangles are right-aligned by the renderer (see above).
        const int texX = (tileType == TileType::LeftTriangle) ? (width - 1 - rawX) : rawX;
        return frameData[TriangleRowOffset(row) + texX];
    }
    default:
        return 0;
    }
}

/**
 * Get the base color for a dungeon wall tile (used when texturing fails)
 */
uint8_t GetWallBaseColor(int textureId, bool isDoor) {
    if (isDoor)
        return 80;
    return static_cast<uint8_t>(64 + (textureId % 128));
}

// Input state
struct InputState {
    bool forward = false;
    bool backward = false;
    bool strafeLeft = false;
    bool strafeRight = false;
    bool up = false;
    bool down = false;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
} g_input;

// Movement speed (tiles per second)
constexpr float MOVE_SPEED = 5.0f;
constexpr float TURN_SPEED = 2.0f; // radians per second

// Mouse look sensitivity (radians per mouse-motion unit)
constexpr float MOUSE_SENSITIVITY = 0.005f;

// Raycast result
struct RayHit {
    float distance = FAR_PLANE;
    int tileX = -1;
    int tileY = -1;
    int side = 0; // 0 = X side, 1 = Y side
    bool hitWall = false;
    bool hitDoor = false;
    int textureId = 0;
    float textureX = 0.0f;
    int lightTableIndex = 0;
    TileType tileType = TileType::Square;
    uint16_t pieceId = 0; // dPiece id of the hit tile, for wall-stack lookup
};

/**
 * Check if a tile is solid (wall)
 */
bool IsWallTile(int x, int y) {
    if (x < 0 || x >= DUNGEON_WIDTH || y < 0 || y >= DUNGEON_HEIGHT)
        return true; // Out of bounds = solid
    return TileHasAny({ x, y }, TileProperties::Solid);
}

/**
 * Check if a tile has a door/special
 */
bool IsDoorTile(int x, int y) {
    if (x < 0 || x >= DUNGEON_WIDTH || y < 0 || y >= DUNGEON_HEIGHT)
        return false;
    return dSpecial[x][y] != 0;
}

/**
 * Get light table index for a tile
 */
int GetTileLight(int x, int y) {
    if (x < 0 || x >= DUNGEON_WIDTH || y < 0 || y >= DUNGEON_HEIGHT)
        return 0;
    return dLight[x][y];
}

/**
 * Draw a scaled CLX sprite as a billboard using direct scaled blitting.
 * This implements a simple nearest-neighbor scaling by iterating the destination
 * and sampling from the source sprite using the CLX RLE decoder.
 */
void DrawScaledSprite(const Surface &out, const ClxSprite &sprite, int screenX, int screenY,
                      float distance, const uint8_t *lightTable, int screenWidth, int screenHeight) {
    if (distance <= 0.1f)
        return;

    // Scale sprite based on distance
    constexpr float BASE_SCALE = 1.5f;
    float scale = BASE_SCALE * (screenHeight / 200.0f) / distance;
    scale = std::clamp(scale, 0.1f, 5.0f);

    int spriteWidth = static_cast<int>(sprite.width() * scale);
    int spriteHeight = static_cast<int>(sprite.height() * scale);

    if (spriteWidth <= 0 || spriteHeight <= 0)
        return;

    // Center horizontally, bottom align vertically
    int drawX = screenX - spriteWidth / 2;
    int drawY = screenY - spriteHeight;

    // Check if completely off-screen
    if (drawX + spriteWidth < 0 || drawX >= screenWidth ||
        drawY + spriteHeight < 0 || drawY >= screenHeight)
        return;

    // Simple approach: decode the CLX sprite to a temporary buffer at native size,
    // then scale-blit to the destination.
    // For CLX sprites (RLE encoded), we need to decode on-the-fly or use a cache.
    // For simplicity, we'll implement a basic scaled blit by decoding the sprite
    // to a temporary buffer first.

    int srcWidth = sprite.width();
    int srcHeight = sprite.height();
    const uint8_t *spriteData = sprite.pixelData();
    uint32_t spriteDataSize = sprite.pixelDataSize();

    // Create a temporary buffer for the decoded sprite
    std::vector<uint8_t> tempBuffer(srcWidth * srcHeight, 0);

    // Decode the CLX sprite to the temp buffer (bottom-up, like the renderer does).
    // CLX encoding (see utils/clx_decode.hpp): control >= 0x80 is OPAQUE
    // (fill run of 0xBF-control pixels if control <= 0xBE, else literal run
    // of -control pixels); control < 0x80 is a TRANSPARENT run of pixels.
    const uint8_t *src = spriteData;
    const uint8_t *srcEnd = src + spriteDataSize;

    // Decode row by row from bottom to top (CL2 format is bottom-up)
    for (int y = srcHeight - 1; y >= 0; --y) {
        int x = 0;
        while (x < srcWidth && src < srcEnd) {
            uint8_t control = *src++;
            if (control >= 0x80) { // Opaque
                if (control <= 0xBE) { // Fill run
                    const int run = 0xBF - control;
                    if (src >= srcEnd) break;
                    const uint8_t color = *src++;
                    for (int i = 0; i < run && x < srcWidth; ++i, ++x) {
                        tempBuffer[y * srcWidth + x] = color;
                    }
                } else { // Literal pixels
                    int run = 256 - control;
                    for (int i = 0; i < run && x < srcWidth; ++i, ++x) {
                        if (src >= srcEnd) break;
                        tempBuffer[y * srcWidth + x] = *src++;
                    }
                }
            } else { // Transparent run
                x += control;
                if (x > srcWidth) break;
            }
        }
    }

    // Now scale-blit the decoded temp buffer to the screen
    float xRatio = static_cast<float>(srcWidth) / static_cast<float>(spriteWidth);
    float yRatio = static_cast<float>(srcHeight) / static_cast<float>(spriteHeight);

    // Clip to screen bounds
    int clipLeft = std::max(0, -drawX);
    int clipTop = std::max(0, -drawY);
    int clipRight = std::min(spriteWidth, screenWidth - drawX);
    int clipBottom = std::min(spriteHeight, screenHeight - drawY);

    for (int dy = clipTop; dy < clipBottom; ++dy) {
        int sy = std::min(static_cast<int>((dy + clipTop) * yRatio), srcHeight - 1);
        for (int dx = clipLeft; dx < clipRight; ++dx) {
            int sx = std::min(static_cast<int>((dx + clipLeft) * xRatio), srcWidth - 1);
            uint8_t color = tempBuffer[sy * srcWidth + sx];
            if (color != 0) { // 0 = transparent
                if (lightTable != nullptr)
                    color = lightTable[color];
                int dstX = drawX + dx;
                int dstY = drawY + dy;
                if (dstX >= 0 && dstX < screenWidth && dstY >= 0 && dstY < screenHeight) {
                    *out.at(dstX, dstY) = color;
                }
            }
        }
    }
}

/**
 * Place the camera at the center of the given tile, nudging to the
 * nearest walkable tile when the target itself is not walkable.
 * Uses the game's own walkability rules (solid tiles, objects, doors).
 */
void SnapCameraToTile(Point tile)
{
    Point placed = tile;
    if (!InDungeonBounds(placed) || !IsTileWalkable(placed)) {
        bool found = false;
        for (int radius = 1; radius <= 8 && !found; radius++) {
            for (int dy = -radius; dy <= radius && !found; dy++) {
                for (int dx = -radius; dx <= radius && !found; dx++) {
                    if (std::max(std::abs(dx), std::abs(dy)) != radius)
                        continue;
                    const Point candidate { tile.x + dx, tile.y + dy };
                    if (InDungeonBounds(candidate) && IsTileWalkable(candidate)) {
                        placed = candidate;
                        found = true;
                    }
                }
            }
        }
        if (!found)
            return; // Nowhere valid nearby; keep the current camera position.
    }
    g_camera.x = static_cast<float>(placed.x) + 0.5f;
    g_camera.y = static_cast<float>(placed.y) + 0.5f;
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;
}

/**
 * Initialize camera from ViewPosition
 */
void InitializeCamera(Point viewPosition) {
    if (!g_cameraInitialized) {
        g_camera.x = static_cast<float>(viewPosition.x) + 0.5f;
        g_camera.y = static_cast<float>(viewPosition.y) + 0.5f;
        g_camera.yaw = 0.0f; // Facing north initially
        g_camera.pitch = 0.0f;
        g_cameraInitialized = true;
    }
    // In FPS mode, the camera is the authoritative position.
    // Do NOT interpolate toward ViewPosition — that would cancel out movement.
}

/**
 * Update camera based on input.
 *
 * The camera is render-only state: orientation comes from the mouse and
 * the position follows the authoritative player tile. Movement itself is
 * performed by the game simulation via CMD_WALKXY walk commands issued
 * from ProcessFPInput, so collision, doors, multiplayer sync, occupancy
 * (dPlayer), lighting/vision and ViewPosition all stay authoritative.
 * This function must not write simulation state.
 */
void UpdateCamera(float deltaTime) {
    // Rotation (mouse delta is already in radians, don't multiply by deltaTime)
    g_camera.yaw += g_input.mouseDeltaX;
    g_camera.pitch = std::clamp(g_camera.pitch - g_input.mouseDeltaY, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;

    // Normalize yaw
    while (g_camera.yaw > M_PI) g_camera.yaw -= 2.0f * M_PI;
    while (g_camera.yaw < -M_PI) g_camera.yaw += 2.0f * M_PI;

    if (MyPlayer == nullptr)
        return;

    // Follow the authoritative player position (tile center), smoothed so
    // tile steps glide instead of snapping. Snap when far away
    // (teleports, level changes, or first entry).
    const float targetX = static_cast<float>(MyPlayer->position.tile.x) + 0.5f;
    const float targetY = static_cast<float>(MyPlayer->position.tile.y) + 0.5f;
    const float dx = targetX - g_camera.x;
    const float dy = targetY - g_camera.y;
    if (dx * dx + dy * dy > 2.25f) { // > 1.5 tiles: snap
        g_camera.x = targetX;
        g_camera.y = targetY;
    } else {
        const float follow = std::min(1.0f, deltaTime * 10.0f);
        g_camera.x += dx * follow;
        g_camera.y += dy * follow;
    }
}

/**
 * Cast a single ray and return hit information
 */
RayHit CastRay(float rayAngle) {
    RayHit hit;
    hit.distance = FAR_PLANE;

    float rayDirX = std::sin(rayAngle);
    float rayDirY = -std::cos(rayAngle);

    // Current tile
    int mapX = static_cast<int>(g_camera.x);
    int mapY = static_cast<int>(g_camera.y);

    // DDA setup
    float deltaDistX = (rayDirX == 0) ? 1e30f : std::abs(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30f : std::abs(1.0f / rayDirY);

    int stepX, stepY;
    float sideDistX, sideDistY;

    if (rayDirX < 0) {
        stepX = -1;
        sideDistX = (g_camera.x - mapX) * deltaDistX;
    } else {
        stepX = 1;
        sideDistX = (mapX + 1.0f - g_camera.x) * deltaDistX;
    }

    if (rayDirY < 0) {
        stepY = -1;
        sideDistY = (g_camera.y - mapY) * deltaDistY;
    } else {
        stepY = 1;
        sideDistY = (mapY + 1.0f - g_camera.y) * deltaDistY;
    }

    // DDA loop
    int side = 0;
    for (int i = 0; i < MAX_RAY_DISTANCE; ++i) {
        if (sideDistX < sideDistY) {
            sideDistX += deltaDistX;
            mapX += stepX;
            side = 0;
        } else {
            sideDistY += deltaDistY;
            mapY += stepY;
            side = 1;
        }

        if (IsWallTile(mapX, mapY)) {
            hit.hitWall = true;
            hit.hitDoor = IsDoorTile(mapX, mapY);
            hit.tileX = mapX;
            hit.tileY = mapY;
            hit.side = side;
            hit.distance = (side == 0) ? sideDistX - deltaDistX : sideDistY - deltaDistY;
            hit.lightTableIndex = GetTileLight(mapX, mapY);

            // Calculate texture coordinate
            float wallX;
            if (side == 0)
                wallX = g_camera.y + hit.distance * rayDirY;
            else
                wallX = g_camera.x + hit.distance * rayDirX;
            wallX -= std::floor(wallX);
            hit.textureX = wallX;

            // Get texture from dungeon data
            if (mapX >= 0 && mapX < DUNGEON_WIDTH && mapY >= 0 && mapY < DUNGEON_HEIGHT) {
                uint16_t pieceId = dPiece[mapX][mapY];
                if (pieceId > 0 && pieceId < MAXTILES) {
                    hit.pieceId = pieceId;
                    // Use first micro tile for texture
                    LevelCelBlock block = { DPieceMicros[pieceId].mt[0] };
                    if (block.hasValue()) {
                        hit.textureId = block.frame();
                        hit.tileType = block.type();
                    }
                }
            }
            break;
        }
    }

    return hit;
}

/**
 * Draw a vertical slice (column) of the wall
 */
void DrawWallColumn(const Surface &out, int screenX, const RayHit &hit, int screenWidth, int screenHeight) {
    if (!hit.hitWall || hit.distance <= 0)
        return;

    // Correct for fisheye effect
    float correctedDist = hit.distance * std::cos(g_camera.yaw - (g_camera.yaw - FOV / 2 + (screenX / static_cast<float>(screenWidth)) * FOV));

    // Calculate wall height on screen.
    // The wall spans world heights [0, WALL_HEIGHT]; the eye sits
    // EYE_HEIGHT above the floor, so the base projects EYE_HEIGHT/dist
    // below the horizon and the top (WALL_HEIGHT - EYE_HEIGHT)/dist above.
    // This matches the floor caster, so wall bases meet floors exactly.
    int drawStart = static_cast<int>(screenHeight / 2 - ((WALL_HEIGHT - EYE_HEIGHT) / correctedDist) * screenHeight + g_camera.pitch * screenHeight);
    int drawEnd = static_cast<int>(screenHeight / 2 + (EYE_HEIGHT / correctedDist) * screenHeight + g_camera.pitch * screenHeight);

    drawStart = std::max(0, drawStart);
    drawEnd = std::min(screenHeight - 1, drawEnd);

    if (drawStart >= drawEnd)
        return;

    // Get light table
    const uint8_t *lightTable = (hit.lightTableIndex > 0 && hit.lightTableIndex < static_cast<int>(std::size(LightTables)))
        ? LightTables[hit.lightTableIndex].data()
        : LightTables[0].data();

    // Wall art lives in the micro-tile stack above the floor pair:
    // mt[0],mt[1] = floor triangles, mt[2..MicroTileLen) = wall layers
    // (left/right frame pairs going upward, like DrawCell in scrollrt.cpp).
    const int numLayers = MicroTileLen > 2 ? (static_cast<int>(MicroTileLen) - 2) / 2 : 0;

    // Draw vertical line with texture sampling
    for (int y = drawStart; y <= drawEnd; ++y) {
        float v = (y - drawStart) / static_cast<float>(drawEnd - drawStart);
        float u = hit.textureX;

        // Select the wall layer frame for this height.
        // v = 0 at the top of the wall column, v = 1 at the base.
        // mt[2] is the bottom layer, so the top of the screen maps to
        // the topmost layer. Each layer is authored as a left/right frame
        // PAIR drawn side-by-side by DrawCell — but real MIN data shows
        // the odd slot is often empty (e.g. L1 pieces 0, 2, 12), in which
        // case the single frame spans the whole face (mirrored on the
        // right half so texel density and the center seam stay clean)
        // instead of falling back to a flat color for half the wall.
        const uint8_t *frameData = nullptr;
        TileType frameType = hit.tileType;
        int frameId = hit.textureId;
        if (hit.pieceId > 0 && numLayers > 0) {
            const float h = v * numLayers;
            const int layerFromTop = std::min(static_cast<int>(h), numLayers - 1);
            const int layerFromBottom = numLayers - 1 - layerFromTop;
            const float subV = 1.0f - (h - static_cast<float>(layerFromTop)); // 1 at layer top
            const LevelCelBlock leftBlock { DPieceMicros[hit.pieceId].mt[2 + 2 * layerFromBottom] };
            const LevelCelBlock rightBlock { DPieceMicros[hit.pieceId].mt[2 + 2 * layerFromBottom + 1] };
            const bool leftOk = leftBlock.hasValue() && leftBlock.type() != TileType::TransparentSquare;
            const bool rightOk = rightBlock.hasValue() && rightBlock.type() != TileType::TransparentSquare;
            const bool rightHalf = u >= 0.5f;
            if (leftOk && rightOk) {
                const LevelCelBlock &wallBlock = rightHalf ? rightBlock : leftBlock;
                frameId = wallBlock.frame();
                frameType = wallBlock.type();
                frameData = GetDunFrame(pDungeonCels.get(), frameId);
                v = std::clamp(subV, 0.0f, 1.0f);
                u = std::clamp(rightHalf ? (u - 0.5f) * 2.0f : u * 2.0f, 0.0f, 1.0f);
            } else if (leftOk || rightOk) {
                const LevelCelBlock &wallBlock = leftOk ? leftBlock : rightBlock;
                frameId = wallBlock.frame();
                frameType = wallBlock.type();
                frameData = GetDunFrame(pDungeonCels.get(), frameId);
                v = std::clamp(subV, 0.0f, 1.0f);
                // Mirror across the middle so the seam is continuous.
                u = std::clamp(rightHalf ? 1.0f - (u - 0.5f) * 2.0f : u * 2.0f, 0.0f, 1.0f);
            }
        }
        if (frameData == nullptr && hit.textureId > 0) {
            frameData = GetDunFrame(pDungeonCels.get(), hit.textureId);
        }

        // Sample texture from dungeon CEL data
        uint8_t color;
        if (g_geoDebug) {
            // Geometry-only diagnostic: flat color per wall orientation.
            // Door tiles (real door objects, not dSpecial arches) read green.
            // No textures, no lighting.
            const Object *wallObj = FindObjectAtPosition({ hit.tileX, hit.tileY });
            if (wallObj != nullptr && wallObj->isDoor())
                color = 180;
            else if (hit.side == 0)
                color = 200;
            else
                color = 150;
        } else if (frameData != nullptr && frameType != TileType::TransparentSquare) {
            color = SampleDungeonTexture(frameData, frameType, u, v);
            g_diagWallTex++;
            g_lastWallTile = { hit.tileX, hit.tileY };
            g_lastWallFrame = frameId;
            g_lastWallPiece = hit.pieceId;
        } else {
            // Fallback color
            color = GetWallBaseColor(frameId, hit.hitDoor);
            g_diagWallFallback++;
        }

        // Apply lighting
        if (lightTable != nullptr) {
            color = lightTable[color];
        }

        // Darken based on distance
        float fade = 1.0f - std::min(hit.distance / FAR_PLANE, 1.0f);
        color = static_cast<uint8_t>(color * fade);

        // Darken Y-side walls for depth perception
        if (hit.side == 1)
            color = static_cast<uint8_t>(color * 0.7f);

        *out.at(screenX, y) = color;
    }
}

/**
 * Sample the floor art for a world position.
 * Floors are the mt[0]/mt[1] triangle pair of the tile's piece.
 * Like DrawFloorTile in scrollrt.cpp, the frames are always interpreted
 * as Left/RightTriangle art regardless of the MIN-declared block type,
 * because several floor pieces declare a Square type over triangle data
 * (sampling those as squares reads out of bounds and shows garbage).
 * The left triangle covers the left half of the tile square, the right
 * triangle the right half. Returns 0 when no usable art exists.
 */
uint8_t SampleFloorAt(float worldX, float worldY, int *outLightTableIndex)
{
    const int tileX = static_cast<int>(std::floor(worldX));
    const int tileY = static_cast<int>(std::floor(worldY));
    if (outLightTableIndex != nullptr)
        *outLightTableIndex = 0;
    if (tileX < 0 || tileX >= DUNGEON_WIDTH || tileY < 0 || tileY >= DUNGEON_HEIGHT)
        return 0;
    const uint16_t pieceId = dPiece[tileX][tileY];
    if (pieceId == 0 || pieceId >= MAXTILES)
        return 0;
    if (outLightTableIndex != nullptr)
        *outLightTableIndex = dLight[tileX][tileY];

    const float fracX = worldX - std::floor(worldX);
    const float fracY = worldY - std::floor(worldY);
    // Left triangle covers the left half of the tile, right triangle the right half.
    const bool leftHalf = fracX < 0.5f;
    const LevelCelBlock floorBlock { DPieceMicros[pieceId].mt[leftHalf ? 0 : 1] };
    if (!floorBlock.hasValue() || floorBlock.type() == TileType::TransparentSquare)
        return 0;
    const float u = leftHalf ? fracX * 2.0f : (fracX - 0.5f) * 2.0f;
    // Triangle row 0 is the south tip of the isometric diamond, which sits
    // at the south edge of the top-down tile square (fracY = 1).
    const float v = 1.0f - fracY;
    const TileType triType = leftHalf ? TileType::LeftTriangle : TileType::RightTriangle;
    g_lastFloorTile = { tileX, tileY };
    g_lastFloorFrame = floorBlock.frame();
    return SampleDungeonTexture(GetDunFrame(pDungeonCels.get(), floorBlock.frame()), triType, u, v);
}

/**
 * Draw floor and ceiling with textured rendering
 */
void DrawFloorCeiling(const Surface &out, int screenX, const RayHit &hit, int screenWidth, int screenHeight) {
    int horizon = screenHeight / 2 + static_cast<int>(g_camera.pitch * screenHeight);

    // Light table fallback (per-pixel tables override this below)
    const uint8_t *cameraLightTable = LightTables[0].data();

    for (int y = 0; y < screenHeight; ++y) {
        uint8_t color = 0;
        const uint8_t *lightTable = cameraLightTable;

        if (g_geoDebug) {
            // Geometry-only diagnostic: flat colors, no textures/lighting.
            // Walkable floor = neutral gray, solid = dark, out of bounds =
            // black, door tiles = green, ceiling = near-black.
            float p = (y < horizon) ? (horizon - y) : (y - screenHeight / 2 - g_camera.pitch * screenHeight);
            if (p == 0) p = 1;
            const float rowDistance = (EYE_HEIGHT * screenHeight) / p;
            const float rayAngle = g_camera.yaw - FOV / 2 + (screenX / static_cast<float>(screenWidth)) * FOV;
            const float wx = g_camera.x + std::sin(rayAngle) * rowDistance;
            const float wy = g_camera.y - std::cos(rayAngle) * rowDistance;
            if (y < horizon) {
                color = 8;
            } else {
                const Point tile { static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy)) };
                if (!InDungeonBounds(tile)) {
                    color = 0;
                } else {
                    const Object *obj = FindObjectAtPosition(tile);
                    if (obj != nullptr && obj->isDoor()) {
                        color = 180;
                    } else if (IsTileWalkable(tile)) {
                        color = 96;
                    } else {
                        color = 16;
                    }
                    // Tile-grid overlay: brighten pixels near tile borders
                    // so alignment, corners and doorways can be inspected.
                    const float fx = wx - std::floor(wx);
                    const float fy = wy - std::floor(wy);
                    if (fx < 0.04f || fx > 0.96f || fy < 0.04f || fy > 0.96f) {
                        color = 200;
                    }
                }
            }
            *out.at(screenX, y) = color;
            continue;
        }

        if (y < horizon) {
            // Ceiling rendering.
            // The original game has no ceiling art, so this stays a dark
            // procedural placeholder until a later milestone.
            float p = horizon - y;
            if (p == 0) p = 1;
            float posZ = EYE_HEIGHT * screenHeight;
            float rowDistance = posZ / p;

            // Calculate world position for this ceiling pixel
            float rayAngle = g_camera.yaw - FOV / 2 + (screenX / static_cast<float>(screenWidth)) * FOV;
            float rayDirX = std::sin(rayAngle);
            float rayDirY = -std::cos(rayAngle);

            float floorX = g_camera.x + rayDirX * rowDistance;
            float floorY = g_camera.y + rayDirY * rowDistance;

            // Simple ceiling texture - dark with subtle pattern
            int texX = static_cast<int>(floorX * 16) & 63;
            int texY = static_cast<int>(floorY * 16) & 63;
            color = static_cast<uint8_t>(16 + ((texX + texY) & 15));

            // Distance fade
            float fade = 1.0f - std::min(rowDistance / FAR_PLANE, 1.0f);
            color = static_cast<uint8_t>(color * fade * 0.5f); // Ceiling darker

        } else {
            // Floor rendering
            float p = y - screenHeight / 2 - g_camera.pitch * screenHeight;
            if (p == 0) continue;
            float posZ = EYE_HEIGHT * screenHeight;
            float rowDistance = posZ / p;

            // Calculate world position for this floor pixel
            float rayAngle = g_camera.yaw - FOV / 2 + (screenX / static_cast<float>(screenWidth)) * FOV;
            float rayDirX = std::sin(rayAngle);
            float rayDirY = -std::cos(rayAngle);

            float floorX = g_camera.x + rayDirX * rowDistance;
            float floorY = g_camera.y + rayDirY * rowDistance;

            // Sample the art of the tile under this pixel (per-pixel lookup,
            // so each tile shows its own floor texture, not the camera tile's).
            int lightIdx = 0;
            const uint8_t sampled = SampleFloorAt(floorX, floorY, &lightIdx);
            if (sampled != 0) {
                color = sampled;
                g_diagFloorTex++;
                if (lightIdx > 0 && lightIdx < static_cast<int>(std::size(LightTables)))
                    lightTable = LightTables[lightIdx].data();
            } else {
                // Fallback floor color when the tile has no usable floor art.
                color = 32;
                g_diagFloorFallback++;
            }

            // Distance fade
            float fade = 1.0f - std::min(rowDistance / FAR_PLANE, 1.0f);
            color = static_cast<uint8_t>(color * fade);
        }

        // Apply lighting
        if (lightTable != nullptr) {
            color = lightTable[color];
        }

        *out.at(screenX, y) = color;
    }
}

/**
 * Render the dungeon using raycasting
 */
void RenderDungeonRaycast(const Surface &out) {
    int screenWidth = out.w();
    int screenHeight = out.h();

    for (int x = 0; x < screenWidth; ++x) {
        float rayAngle = g_camera.yaw - FOV / 2 + (x / static_cast<float>(screenWidth)) * FOV;
        RayHit hit = CastRay(rayAngle);

        // Draw floor and ceiling first
        DrawFloorCeiling(out, x, hit, screenWidth, screenHeight);

        // Draw wall if hit
        if (hit.hitWall) {
            DrawWallColumn(out, x, hit, screenWidth, screenHeight);
        }
    }
}

/**
 * Cast a ray from the crosshair center and update targeting variables.
 * This mimics the isometric mode's cursor targeting logic.
 */
void UpdateCrosshairTarget(int screenWidth, int screenHeight) {
    // Cast ray from camera center (crosshair position)
    float rayAngle = g_camera.yaw;
    RayHit hit = CastRay(rayAngle);

    // Reset cursor variables
    pcursitem = -1;
    pcursmonst = -1;
    pcurstrig = -1;
    pcursmissile = nullptr;
    pcursquest = Q_INVALID;
    ObjectUnderCursor = nullptr;
    PlayerUnderCursor = nullptr;
    cursPosition = { -1, -1 };

    if (!hit.hitWall) {
        return;
    }

    // Set the cursor position to the hit tile
    cursPosition = { static_cast<int16_t>(hit.tileX), static_cast<int16_t>(hit.tileY) };

    // Check for items on the hit tile
    if (hit.tileX >= 0 && hit.tileX < DUNGEON_WIDTH && hit.tileY >= 0 && hit.tileY < DUNGEON_HEIGHT) {
        int8_t itemIdx = dItem[hit.tileX][hit.tileY];
        if (itemIdx > 0 && !Items[itemIdx - 1].isEmpty()) {
            pcursitem = itemIdx - 1;
        }
    }

    // Check for objects on the hit tile
    if (hit.tileX >= 0 && hit.tileX < DUNGEON_WIDTH && hit.tileY >= 0 && hit.tileY < DUNGEON_HEIGHT) {
        Object *obj = FindObjectAtPosition({ hit.tileX, hit.tileY });
        if (obj != nullptr && obj->canInteractWith() && !obj->IsDisabled()) {
            ObjectUnderCursor = obj;
        }
    }

    // Check for monsters on the hit tile
    if (hit.tileX >= 0 && hit.tileX < DUNGEON_WIDTH && hit.tileY >= 0 && hit.tileY < DUNGEON_HEIGHT) {
        int8_t monsterIdx = dMonster[hit.tileX][hit.tileY];
        if (monsterIdx != 0) {
            int mi = std::abs(monsterIdx) - 1;
            if (mi >= 0 && mi < MaxMonsters) {
                Monster &monster = Monsters[mi];
                if (!monster.hasNoLife() && (monster.flags & MFLAG_HIDDEN) == 0 && IsTileLit({ hit.tileX, hit.tileY })) {
                    if (!monster.isPlayerMinion()) {
                        pcursmonst = mi;
                    }
                }
            }
        }
    }

    // Check for triggers/portals on the hit tile
    for (int i = 0; i < numtrigs; ++i) {
        if (trigs[i].position.x == hit.tileX && trigs[i].position.y == hit.tileY) {
            pcurstrig = i;
            break;
        }
    }

    // Check for quest locations
    for (auto &quest : Quests) {
        if (quest._qidx != Q_INVALID && currlevel == quest._qlevel && quest._qslvl != 0) {
            if (quest.position.x == hit.tileX && quest.position.y == hit.tileY) {
                pcursquest = quest._qidx;
                break;
            }
        }
    }
}

/**
 * Draw the crosshair at screen center with targeting feedback
 */
void DrawCrosshair(const Surface &out) {
    int screenWidth = out.w();
    int screenHeight = out.h();
    int centerX = screenWidth / 2;
    int centerY = screenHeight / 2;
    int crosshairSize = 8;
    uint8_t color = 200; // Bright color

    // Change crosshair color based on what we're targeting
    if (pcursmonst != -1) {
        color = 220; // Red-ish for monsters
    } else if (pcursitem != -1) {
        color = 240; // Yellow-ish for items
    } else if (ObjectUnderCursor != nullptr) {
        color = 180; // Green-ish for objects (doors, chests)
    } else if (cursPosition.x >= 0) {
        color = 200; // Default for walls
    }

    // Horizontal line
    for (int x = centerX - crosshairSize; x <= centerX + crosshairSize; ++x) {
        if (x != centerX && x >= 0 && x < out.w() && centerY >= 0 && centerY < out.h()) {
            *out.at(x, centerY) = color;
        }
    }
    // Vertical line
    for (int y = centerY - crosshairSize; y <= centerY + crosshairSize; ++y) {
        if (y != centerY && centerX >= 0 && centerX < out.w() && y >= 0 && y < out.h()) {
            *out.at(centerX, y) = color;
        }
    }
    // Center dot
    if (centerX >= 0 && centerX < out.w() && centerY >= 0 && centerY < out.h()) {
        *out.at(centerX, centerY) = color;
    }
}

/**
 * Project a world position to screen coordinates
 * @param worldX World X position (tile units)
 * @param worldY World Y position (tile units)
 * @param screenWidth Screen width
 * @param screenHeight Screen height
 * @param outX Output screen X
 * @param outY Output screen Y
 * @param outDistance Output distance from camera
 * @return true if point is in front of camera and within FOV
 */
bool ProjectWorldToScreen(float worldX, float worldY, int screenWidth, int screenHeight,
                          int &outX, int &outY, float &outDistance) {
    // Vector from camera to object
    float dx = worldX - g_camera.x;
    float dy = worldY - g_camera.y;

    // Rotate world offset into camera space.
    // Camera forward is F = (sin yaw, -cos yaw) and right is
    // R = (cos yaw, sin yaw), so depth = rel.F, lateral = rel.R.
    // Depth must be positive ahead of the camera.
    float cosYaw = std::cos(g_camera.yaw);
    float sinYaw = std::sin(g_camera.yaw);
    float rotX = dx * cosYaw + dy * sinYaw;
    float rotY = dx * sinYaw - dy * cosYaw;

    // Check if behind camera
    if (rotY <= 0.1f)  // Small epsilon to avoid division by zero
        return false;

    outDistance = rotY;

    // Calculate screen X from horizontal angle
    float angle = std::atan2(rotX, rotY);
    if (std::abs(angle) > FOV / 2.0f + 0.2f)  // Small margin for sprite width
        return false;

    outX = static_cast<int>(screenWidth / 2 + (angle / (FOV / 2.0f)) * (screenWidth / 2.0f));

    // Ground the sprite: project the floor point under the entity so the
    // billboard base sits on the floor at its distance (eye height above
    // floor divided by depth), plus the pitch offset.
    outY = static_cast<int>(screenHeight / 2 + g_camera.pitch * screenHeight
        + (EYE_HEIGHT / outDistance) * screenHeight);

    return true;
}

/**
 * Render all visible sprites (monsters, players, items) as billboards
 */
void RenderSprites(const Surface &out) {
    int screenWidth = out.w();
    int screenHeight = out.h();

    // Render monsters
    for (int i = 0; i < static_cast<int>(ActiveMonsterCount); ++i) {
        const Monster &monster = Monsters[ActiveMonsters[i]];
        if (monster.hasNoLife() || (monster.flags & MFLAG_HIDDEN) != 0)
            continue;

        int screenX, screenY;
        float distance;
        if (!ProjectWorldToScreen(
                static_cast<float>(monster.position.tile.x) + 0.5f,
                static_cast<float>(monster.position.tile.y) + 0.5f,
                screenWidth, screenHeight, screenX, screenY, distance))
            continue;

        // Get current sprite and light table
        const ClxSprite &sprite = monster.animInfo.currentSprite();

        const uint8_t *lightTable = LightTables[0].data();
        int lightIdx = GetTileLight(monster.position.tile.x, monster.position.tile.y);
        if (lightIdx > 0 && lightIdx < static_cast<int>(std::size(LightTables)))
            lightTable = LightTables[lightIdx].data();

        // Draw scaled sprite with lighting
        DrawScaledSprite(out, sprite, screenX, screenY, distance, lightTable, screenWidth, screenHeight);
    }

    // Render items on ground
    for (int x = 0; x < DUNGEON_WIDTH; ++x) {
        for (int y = 0; y < DUNGEON_HEIGHT; ++y) {
            int8_t itemIdx = dItem[x][y];
            if (itemIdx <= 0) continue;

            const Item &item = Items[itemIdx - 1];
            if (item.isEmpty()) continue;

            int screenX, screenY;
            float distance;
            if (!ProjectWorldToScreen(
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    screenWidth, screenHeight, screenX, screenY, distance))
                continue;

            // Use item's sprite
            const ClxSprite &sprite = item.AnimInfo.currentSprite();

            int lightIdx = GetTileLight(x, y);
            const uint8_t *lightTable = (lightIdx > 0 && lightIdx < static_cast<int>(std::size(LightTables)))
                ? LightTables[lightIdx].data() : LightTables[0].data();

            // Draw scaled sprite
            DrawScaledSprite(out, sprite, screenX, screenY, distance, lightTable, screenWidth, screenHeight);
        }
    }

    // Render other players (multiplayer)
    for (const Player &player : Players) {
        if (&player == MyPlayer || player.hasNoLife() || !player.isOnActiveLevel())
            continue;

        int screenX, screenY;
        float distance;
        if (!ProjectWorldToScreen(
                static_cast<float>(player.position.tile.x) + 0.5f,
                static_cast<float>(player.position.tile.y) + 0.5f,
                screenWidth, screenHeight, screenX, screenY, distance))
            continue;

        const ClxSprite &sprite = player.currentSprite();

        int lightIdx = GetTileLight(player.position.tile.x, player.position.tile.y);
        const uint8_t *lightTable = (lightIdx > 0 && lightIdx < static_cast<int>(std::size(LightTables)))
            ? LightTables[lightIdx].data() : LightTables[0].data();

        // Draw scaled sprite
        DrawScaledSprite(out, sprite, screenX, screenY, distance, lightTable, screenWidth, screenHeight);
    }
}

/**
 * Convert camera yaw to the nearest game direction.
 */
Direction YawToDirection(float yaw) {
    while (yaw < 0) yaw += 2.0f * M_PI;
    while (yaw >= 2.0f * M_PI) yaw -= 2.0f * M_PI;

    const float sector = M_PI / 4.0f;
    int sectorIdx = static_cast<int>((yaw + sector / 2.0f) / sector) % 8;

    static const Direction sectorToDir[8] = {
        Direction::North,
        Direction::NorthEast,
        Direction::East,
        Direction::SouthEast,
        Direction::South,
        Direction::SouthWest,
        Direction::West,
        Direction::NorthWest
    };

    return sectorToDir[sectorIdx];
}

void SendFPWalkCommand(Direction dir) {
    if (MyPlayer == nullptr)
        return;

    // Calculate target tile based on direction
    Point target = MyPlayer->position.tile;
    switch (dir) {
        case Direction::North:    target.y -= 1; break;
        case Direction::South:    target.y += 1; break;
        case Direction::West:     target.x -= 1; break;
        case Direction::East:     target.x += 1; break;
        case Direction::NorthWest: target.x -= 1; target.y -= 1; break;
        case Direction::NorthEast: target.x += 1; target.y -= 1; break;
        case Direction::SouthWest: target.x -= 1; target.y += 1; break;
        case Direction::SouthEast: target.x += 1; target.y += 1; break;
        default: return;
    }

    // Check if target is valid
    if (InDungeonBounds(target) && PosOkPlayer(*MyPlayer, target)) {
        NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, target);
    }
}
/**
 * Recompute the walk target from the currently held WASD keys and issue
 * a single-step walk command in the camera-relative direction.
 * Called on every WASD press/release so combined keys produce diagonals
 * and releasing a key continues with the remaining held keys.
 * Skipped when already heading to that tile to avoid command spam from
 * OS key repeat.
 */
void RefreshFPWalkTarget()
{
    if (MyPlayer == nullptr)
        return;
    float dx = 0.0f;
    float dy = 0.0f;
    const float cosYaw = std::cos(g_camera.yaw);
    const float sinYaw = std::sin(g_camera.yaw);
    if (g_input.forward) {
        dx += sinYaw;
        dy -= cosYaw;
    }
    if (g_input.backward) {
        dx -= sinYaw;
        dy += cosYaw;
    }
    if (g_input.strafeLeft) {
        dx -= cosYaw;
        dy -= sinYaw;
    }
    if (g_input.strafeRight) {
        dx += cosYaw;
        dy += sinYaw;
    }
    if (dx == 0.0f && dy == 0.0f)
        return;
    const Point from = MyPlayer->position.future;
    const Point target { from.x + (dx > 0.5f ? 1 : (dx < -0.5f ? -1 : 0)),
        from.y + (dy > 0.5f ? 1 : (dy < -0.5f ? -1 : 0)) };
    if (target == from || target == MyPlayer->position.future)
        return;
    if (InDungeonBounds(target) && PosOkPlayer(*MyPlayer, target)) {
        NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, target);
    }
}
void ProcessFPInput(const SDL_Event &event) {
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        bool pressed = (event.type == SDL_KEYDOWN);
        SDL_Keycode key = event.key.keysym.sym;

        switch (key) {
            case SDLK_w: g_input.forward = pressed; RefreshFPWalkTarget(); break;
            case SDLK_s: g_input.backward = pressed; RefreshFPWalkTarget(); break;
            case SDLK_a: g_input.strafeLeft = pressed; RefreshFPWalkTarget(); break;
            case SDLK_d: g_input.strafeRight = pressed; RefreshFPWalkTarget(); break;
            case SDLK_SPACE: g_input.up = pressed; break;
            case SDLK_LCTRL: g_input.down = pressed; break;
        }
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        // Always process mouse motion for FPS-style mouselook
        // When in relative mode, xrel/yrel are already relative movements
        // Accumulate deltas — multiple MOUSEMOTION events can arrive per frame
        if (g_relativeMouseMode || g_camera.mouseLookEnabled) {
            g_input.mouseDeltaX += static_cast<float>(event.motion.xrel) * MOUSE_SENSITIVITY;
            g_input.mouseDeltaY += static_cast<float>(event.motion.yrel) * MOUSE_SENSITIVITY;
        }
    }
}

} // namespace

/**
 * Log FPS texture diagnostics: which frames the renderer resolves and
 * how many pixels use real art vs. fallback colors.
 * Distinguishes missing assets / decode failures (fallback dominates,
 * frame 0, empty wall stacks) from mapping problems (frames resolve
 * but look wrong on screen).
 */
void LogFpTextureStats()
{
    std::string playerPiece = "none";
    std::string wallStack = "none";
    if (MyPlayer != nullptr) {
        const Point tile = MyPlayer->position.tile;
        if (InDungeonBounds(tile)) {
            const uint16_t pieceId = dPiece[tile.x][tile.y];
            if (pieceId > 0 && pieceId < MAXTILES) {
                const LevelCelBlock f0 { DPieceMicros[pieceId].mt[0] };
                const LevelCelBlock f1 { DPieceMicros[pieceId].mt[1] };
                playerPiece = "piece " + std::to_string(pieceId)
                    + " mt0 f" + std::to_string(f0.hasValue() ? f0.frame() : 0)
                    + "/t" + std::to_string(static_cast<int>(f0.type()))
                    + " mt1 f" + std::to_string(f1.hasValue() ? f1.frame() : 0)
                    + "/t" + std::to_string(static_cast<int>(f1.type()));
                std::string layers;
                for (int i = 2; i < static_cast<int>(MicroTileLen) && i < 16; i++) {
                    const LevelCelBlock b { DPieceMicros[pieceId].mt[i] };
                    layers += (i > 2 ? "," : "") + std::to_string(b.hasValue() ? b.frame() : 0);
                }
                wallStack = "mt[2..] frames [" + layers + "]";
            } else {
                playerPiece = "piece " + std::to_string(pieceId) + " (invalid)";
            }
        }
    }
    LogInfo("FPS tex: level {} microLen {} player {} | walls art/fallback {}/{} floors art/fallback {}/{} | last wall tile({},{}) piece {} frame {} last floor tile({},{}) frame {}",
        static_cast<int>(currlevel), static_cast<int>(MicroTileLen), playerPiece,
        g_diagWallTex, g_diagWallFallback, g_diagFloorTex, g_diagFloorFallback,
        g_lastWallTile.x, g_lastWallTile.y, g_lastWallPiece, g_lastWallFrame,
        g_lastFloorTile.x, g_lastFloorTile.y, g_lastFloorFrame);
    g_diagWallTex = g_diagWallFallback = g_diagFloorTex = g_diagFloorFallback = 0;
}

void DrawFirstPersonView(const Surface &out, Point startPosition) {
    // Initialize/update camera
    InitializeCamera(startPosition);

    // If the level changed underneath FPS mode (stairs, portal, load),
    // the old camera coordinates belong to another map. Reconcile with
    // the authoritative player position instead of rendering stuck
    // inside a wall.
    static uint8_t lastLevel = 0;
    static bool firstFrame = true;
    if (firstFrame || currlevel != lastLevel) {
        firstFrame = false;
        lastLevel = currlevel;
        if (MyPlayer != nullptr)
            SnapCameraToTile(MyPlayer->position.tile);
        LogFpTextureStats();
    }

    // Clear any stale mouse deltas from before entering FPS mode
    // This prevents the camera from jumping on the first frame
    static bool wasInFPS = false;
    if (!wasInFPS) {
        g_input.mouseDeltaX = 0.0f;
        g_input.mouseDeltaY = 0.0f;
        wasInFPS = true;
    }

    // Enable relative mouse mode for FPS-style mouselook
    // This is idempotent — SDL_SetRelativeMouseMode(SDL_TRUE) is a no-op if already active
    // scrollrt.cpp disables it when switching back to isometric mode
    g_relativeMouseMode = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    // Get delta time (approximate - using fixed 1/20s for game tick)
    static uint32_t lastTicks = 0;
    uint32_t currentTicks = SDL_GetTicks();
    float deltaTime = (lastTicks > 0) ? (currentTicks - lastTicks) / 1000.0f : 0.05f;
    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f); // Cap at 100ms
    lastTicks = currentTicks;

    UpdateCamera(deltaTime);

    // Clear screen
    FillRect(out, 0, 0, out.w(), out.h(), 0);

    // Render dungeon via raycasting
    RenderDungeonRaycast(out);

    // Render sprites (monsters, items, players) as billboards
    RenderSprites(out);

    // Update crosshair targeting (sets cursPosition, pcursmonst, pcursitem, etc.)
    UpdateCrosshairTarget(out.w(), out.h());

    // Draw crosshair at screen center
    DrawCrosshair(out);

    // Debug info
    DrawString(out, "FPS MODE | Pos: (" + std::to_string(g_camera.x) + ", " + std::to_string(g_camera.y) + ") Yaw: " + std::to_string(g_camera.yaw),
        Rectangle { Point { 10, 10 }, Size { 400, 16 } },
        { .flags = UiFlags::ColorWhite });

    // Texture diagnostics: textured vs. fallback pixels this session.
    // If fallbacks dominate, art isn't resolving; if textured dominates
    // but looks wrong, the mapping is at fault.
    DrawString(out, "Tex W:" + std::to_string(g_diagWallTex) + "/" + std::to_string(g_diagWallFallback)
            + " F:" + std::to_string(g_diagFloorTex) + "/" + std::to_string(g_diagFloorFallback),
        Rectangle { Point { 10, 66 }, Size { 500, 16 } },
        TextRenderOptions { .flags = UiFlags::ColorBlue });

    // Periodically report to the log (rate-limited).
    static int framesSinceReport = 0;
    if (++framesSinceReport >= 600) {
        framesSinceReport = 0;
        LogFpTextureStats();
    }

    // Show what we're targeting
    std::string targetInfo = "Target: ";
    if (pcursmonst != -1) {
        targetInfo += "Monster [" + std::to_string(pcursmonst) + "]";
    } else if (pcursitem != -1) {
        targetInfo += "Item [" + std::to_string(pcursitem) + "]";
    } else if (ObjectUnderCursor != nullptr) {
        if (ObjectUnderCursor->isDoor()) {
            float dx = g_camera.x - (cursPosition.x + 0.5f);
            float dy = g_camera.y - (cursPosition.y + 0.5f);
            int dist = static_cast<int>(std::sqrt(dx * dx + dy * dy) + 0.5f);
            targetInfo += "DOOR (dist: " + std::to_string(dist) + ")";
        } else {
            targetInfo += "Object";
        }
    } else if (cursPosition.x >= 0) {
        targetInfo += "Wall";
    } else {
        targetInfo += "None";
    }
    DrawString(out, targetInfo,
        Rectangle { Point { 10, 50 }, Size { 500, 16 } },
        TextRenderOptions { .flags = UiFlags::ColorBlue });

    DrawString(out, "WASD=Move | Mouse=Look | F6=Toggle | LMB=Interact",
        Rectangle { Point { 10, 30 }, Size { 500, 16 } },
        { .flags = UiFlags::ColorYellow });

    if (g_geoDebug && MyPlayer != nullptr) {
        // Geometry inspector: exact tile/piece under the camera and the
        // crosshair, so a broken-looking spot can be reported precisely.
        const Point camTile { static_cast<int>(g_camera.x), static_cast<int>(g_camera.y) };
        const uint16_t camPiece = InDungeonBounds(camTile) ? dPiece[camTile.x][camTile.y] : 0;
        std::string geoInfo = "cam(" + std::to_string(camTile.x) + "," + std::to_string(camTile.y)
            + ")p" + std::to_string(camPiece)
            + " hit(" + std::to_string(cursPosition.x) + "," + std::to_string(cursPosition.y) + ")";
        if (InDungeonBounds(cursPosition)) {
            const uint16_t hitPiece = dPiece[cursPosition.x][cursPosition.y];
            geoInfo += "p" + std::to_string(hitPiece);
        }
        DrawString(out, geoInfo,
            Rectangle { Point { 10, 82 }, Size { 500, 16 } },
            TextRenderOptions { .flags = UiFlags::ColorBlue });
    }
}

/**
 * Handle input events for first-person mode
 * Call this from the main event loop
 */
void HandleFirstPersonInput(const SDL_Event &event) {
    ProcessFPInput(event);

    // Handle LMB click for interaction in FP mode
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        // Trigger interaction at crosshair position
        if (cursPosition.x >= 0 && cursPosition.y >= 0) {
            // Check if we're close enough to interact (for doors, need to be adjacent)
            float dx = g_camera.x - (cursPosition.x + 0.5f);
            float dy = g_camera.y - (cursPosition.y + 0.5f);
            float distSq = dx * dx + dy * dy;

            // For doors, require being within ~1.5 tiles
            bool canInteract = true;
            if (ObjectUnderCursor != nullptr && ObjectUnderCursor->isDoor()) {
                canInteract = (distSq <= 2.25f); // 1.5 tiles squared
            }

            if (canInteract) {
                // Call the same interaction logic as isometric mode
                PerformPrimaryAction();
            }
        }
    }
}

void ClearFirstPersonMouseDeltas()
{
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;
}

void ToggleFirstPersonGeometryDebug()
{
    g_geoDebug = !g_geoDebug;
    // Discard stale look deltas so toggling never jerks the camera.
    ClearFirstPersonMouseDeltas();
}

void OnEnterFirstPersonMode()
{
    // Reconcile the camera with the authoritative player position so FPS
    // mode never begins with the camera embedded in solid geometry
    // (e.g. stale coordinates from another level).
    // Orientation is intentionally preserved across toggles.
    if (MyPlayer != nullptr)
        SnapCameraToTile(MyPlayer->position.tile);
}

} // namespace devilution
