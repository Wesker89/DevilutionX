/**
 * @file fp_texture.hpp
 *
 * Texture access for the first-person raycaster.
 *
 * Diablo's dungeon art is not stored as plain bitmaps. Every "frame" is a 32px wide
 * piece of an isometric tile (square, RLE square, triangle or trapezoid, see TileType).
 * The raycaster needs ordinary linear bitmaps it can index with (x, y), so this module:
 *
 *   1. Decodes each frame ONCE into a flat 32x32 bitmap (+ transparency mask), cached.
 *   2. Maps wall hits / floor points onto the original isometric sprite layout
 *      (undoing the 2:1 iso shear) and reads texels from those bitmaps.
 */
#pragma once

#include <cstdint>

#include "levels/dun_tile.hpp"

namespace devilution {

/** A decoded 32x32 dungeon frame, stored top-down (row 0 = top). */
struct FpDecodedTile {
	uint8_t pix[32 * 32];
	uint8_t mask[32 * 32]; // 1 = opaque, 0 = transparent / outside the shape
};

/**
 * @brief Decodes a raw (already "reencoded") dungeon frame into a linear bitmap.
 *
 * Pure function: only touches `src` (at most 1024 bytes) and `out`.
 */
void FpDecodeDunFrame(const uint8_t *src, TileType type, FpDecodedTile &out);

/** Drop all cached decoded frames. Call on level change / when pDungeonCels is reloaded. */
void FpTextureReset();

/**
 * Height in source pixels of wall art above the floor line for the current level
 * (piece height minus the 16px the sloped floor edge takes up). One tile of world
 * height is 32 art pixels, so world wall height = FpWallPixelHeight() / 32.
 */
int FpWallPixelHeight();

/**
 * @brief Sample the floor at a point inside a tile.
 *
 * Engine convention: +x runs down-right and +y down-left on the isometric screen.
 *
 * @param pieceId dPiece value of the tile
 * @param a fractional x inside the tile, [0,1)
 * @param b fractional y inside the tile, [0,1)
 * @return false when the tile has no floor art
 */
bool FpSampleFloor(uint16_t pieceId, float a, float b, uint8_t &color);

/**
 * @brief Sample a wall face.
 *
 * A piece is two 32px columns. The LEFT column is the tile's y = 1 ("south") face and the
 * RIGHT column its x = 1 ("east") face; those are the only faces the isometric camera
 * ever sees. The caller picks the column and the horizontal position inside it:
 *
 *   south face: left  column, u = fractional x
 *   north face: left  column, u = 1 - fractional x   (viewed from behind, same art)
 *   east  face: right column, u = 1 - fractional y
 *   west  face: right column, u = fractional y
 *
 * @param pieceId dPiece value of the (solid) tile that was hit
 * @param rightColumn true for the east/west faces
 * @param u horizontal position inside the 32px column, [0,1)
 * @param heightPx height above the floor in art pixels (world height * 32)
 * @return false when no art exists at that spot
 */
bool FpSampleWall(uint16_t pieceId, bool rightColumn, float u, float heightPx, uint8_t &color);

} // namespace devilution
