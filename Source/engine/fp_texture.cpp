/**
 * @file fp_texture.cpp
 *
 * See fp_texture.hpp.
 *
 * Frame layouts (all stored bottom row first, see levels/dun_tile.hpp):
 *   Square             1024 bytes, 32 rows x 32
 *   TransparentSquare  RLE, runs never cross rows
 *   LeftTriangle       512 bytes, 31 rows, widths 2,4..30,32,30..4,2, right-aligned
 *   RightTriangle      512 bytes, same widths, left-aligned
 *   Left/RightTrapezoid 784 bytes: lower 16 triangle rows (272 bytes) + 32x16 rectangle (512 bytes)
 */
#include "engine/render/fp_texture.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#ifndef FP_TEXTURE_TEST
#include "engine/render/dun_render.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#endif

namespace devilution {

namespace {

constexpr int FrameSize = 32;

void ClearTile(FpDecodedTile &out)
{
	std::memset(out.pix, 0, sizeof(out.pix));
	std::memset(out.mask, 0, sizeof(out.mask));
}

/** Row `i` counted from the bottom lives at y = 31 - i (top-down buffer). */
void PutRow(FpDecodedTile &out, int i, int x, const uint8_t *src, int width)
{
	const int y = FrameSize - 1 - i;
	if (y < 0 || y >= FrameSize)
		return;
	for (int k = 0; k < width; ++k) {
		const int xx = x + k;
		if (xx < 0 || xx >= FrameSize)
			continue;
		out.pix[y * FrameSize + xx] = src[k];
		out.mask[y * FrameSize + xx] = 1;
	}
}

/**
 * Decode the triangle rows [0, rows) starting at bottom row 0 and return the number of source bytes consumed.
 * Widths: i < 16 -> 2*(i+1); i >= 16 -> 2*(31-i).
 */
int DecodeTriangleRows(FpDecodedTile &out, const uint8_t *src, int rows, bool rightAligned)
{
	int consumed = 0;
	for (int i = 0; i < rows; ++i) {
		const int width = i < 16 ? 2 * (i + 1) : 2 * (31 - i);
		const int x = rightAligned ? FrameSize - width : 0;
		PutRow(out, i, x, src + consumed, width);
		consumed += width;
	}
	return consumed;
}

} // namespace

void FpDecodeDunFrame(const uint8_t *src, TileType type, FpDecodedTile &out)
{
	ClearTile(out);
	if (src == nullptr)
		return;

	switch (type) {
	case TileType::Square:
		for (int i = 0; i < FrameSize; ++i)
			PutRow(out, i, 0, src + i * FrameSize, FrameSize);
		break;

	case TileType::TransparentSquare:
		for (int i = 0; i < FrameSize; ++i) {
			int x = 0;
			while (x < FrameSize) {
				const int8_t run = static_cast<int8_t>(*src++);
				if (run > 0) {
					const int n = std::min<int>(run, FrameSize - x);
					PutRow(out, i, x, src, n);
					src += run; // always consume the full run from the stream
					x += run;
				} else if (run < 0) {
					x -= run;
				} else {
					// A zero-length run would never advance; treat it as end of row.
					break;
				}
			}
		}
		break;

	case TileType::LeftTriangle:
		DecodeTriangleRows(out, src, 31, /*rightAligned=*/true);
		break;

	case TileType::RightTriangle:
		DecodeTriangleRows(out, src, 31, /*rightAligned=*/false);
		break;

	case TileType::LeftTrapezoid:
	case TileType::RightTrapezoid: {
		const bool right = type == TileType::LeftTrapezoid; // "Left" art is right-aligned
		const int used = DecodeTriangleRows(out, src, 16, right);
		const uint8_t *rect = src + used; // 272 bytes in
		for (int k = 0; k < 16; ++k)
			PutRow(out, 16 + k, 0, rect + k * FrameSize, FrameSize);
		break;
	}
	}
}

#ifndef FP_TEXTURE_TEST

namespace {

// Cache key: 15 bits of LevelCelBlock::data (type + frame), plus bit 15 for "forced triangle" floors.
constexpr size_t CacheSize = 1 << 16;
std::unique_ptr<FpDecodedTile> g_cache[CacheSize];
const std::byte *g_cacheCels = nullptr;

void SyncCache()
{
	// Dungeon art is reloaded on every level change; stale pointers must never be used.
	if (g_cacheCels != pDungeonCels.get()) {
		FpTextureReset();
		g_cacheCels = pDungeonCels.get();
	}
}

const FpDecodedTile *GetTile(LevelCelBlock block, bool forceTriangle, TileType forcedType)
{
	if (!block.hasValue() || pDungeonCels == nullptr)
		return nullptr;
	SyncCache();

	const TileType type = forceTriangle ? forcedType : block.type();
	const size_t key = (static_cast<size_t>(block.data) & 0x7FFF) | (forceTriangle ? 0x8000u : 0u);
	auto &slot = g_cache[key];
	if (slot == nullptr) {
		slot = std::make_unique<FpDecodedTile>();
		FpDecodeDunFrame(GetDunFrame(pDungeonCels.get(), block.frame()), type, *slot);
	}
	return slot.get();
}

inline bool Texel(const FpDecodedTile *tile, int x, int y, uint8_t &color)
{
	if (tile == nullptr || x < 0 || x >= FrameSize || y < 0 || y >= FrameSize)
		return false;
	if (tile->mask[y * FrameSize + x] == 0)
		return false;
	color = tile->pix[y * FrameSize + x];
	return true;
}

inline int PieceRows()
{
	return std::max<int>(1, static_cast<int>(MicroTileLen) / 2);
}

} // namespace

void FpTextureReset()
{
	for (auto &slot : g_cache)
		slot.reset();
	g_cacheCels = pDungeonCels.get();
}

int FpWallPixelHeight()
{
	// The sloped floor edge eats up to 16px at the top of the sprite.
	return PieceRows() * FrameSize - 16;
}

bool FpSampleFloor(uint16_t pieceId, float a, float b, uint8_t &color)
{
	if (pieceId == 0 || pieceId >= MAXTILES)
		return false;

	a = std::clamp(a, 0.0f, 0.9999f);
	b = std::clamp(b, 0.0f, 0.9999f);

	// Iso projection of the tile footprint onto the 64x32 diamond:
	//   x grows toward screen bottom-left, y toward bottom-right.
	const float px = 32.0f + (b - a) * 32.0f; // 0..64
	const float py = (a + b) * 16.0f;         // 0..32 inside the bottom 32px row

	const int column = px < 32.0f ? 0 : 1;
	const int x = std::clamp(static_cast<int>(px) - column * 32, 0, FrameSize - 1);
	const int y = std::clamp(static_cast<int>(py), 1, FrameSize - 1); // triangles have no row 0

	// The original floor pass (DrawFloorTile) always draws mt[0]/mt[1] as left/right triangles.
	const FpDecodedTile *tile = GetTile(LevelCelBlock { DPieceMicros[pieceId].mt[column] }, true,
	    column == 0 ? TileType::LeftTriangle : TileType::RightTriangle);

	// Triangle edges are jagged; probe a couple of neighbours so we never punch holes.
	static constexpr int Dx[] = { 0, -1, 1, 0, 0, -2, 2 };
	static constexpr int Dy[] = { 0, 0, 0, -1, 1, 0, 0 };
	for (int k = 0; k < 7; ++k) {
		if (Texel(tile, x + Dx[k], y + Dy[k], color))
			return true;
	}
	return false;
}

bool FpSampleWall(uint16_t pieceId, bool yFace, float u, float t, uint8_t &color)
{
	if (pieceId == 0 || pieceId >= MAXTILES)
		return false;

	const int rows = PieceRows();
	const int height = rows * FrameSize; // sprite height H
	const int wallPx = FpWallPixelHeight();

	u = std::clamp(u, 0.0f, 0.9999f);
	t = std::clamp(t, 0.0f, 1.0f);
	const int xc = static_cast<int>(u * FrameSize); // 0..31 inside the micro column

	// A face is one 32px micro column. Its base line is the diamond edge, which slopes 1:2 in the sprite:
	//   left column  (x-face): edge runs from (0, H-16) down to (32, H)
	//   right column (y-face): edge runs from (32, H) up to (64, H-16)
	// "Unshearing" = moving up the wall by t pixels from that sloped base.
	const float yBase = yFace ? height - 0.5f * xc : height - 16.0f + 0.5f * xc;
	const int column = yFace ? 1 : 0;

	for (int attempt = 0; attempt < 2; ++attempt) {
		const int col = attempt == 0 ? column : 1 - column;
		for (int dy = 0; dy < 4; ++dy) { // nudge down a few px if we land on a transparent pixel
			const int yTop = std::clamp(static_cast<int>(yBase - t * wallPx) + dy, 0, height - 1);
			const int row = (height - 1 - yTop) / FrameSize; // 0 = bottom micro row
			const int yIn = yTop - (height - FrameSize * (row + 1));
			const FpDecodedTile *tile = GetTile(LevelCelBlock { DPieceMicros[pieceId].mt[row * 2 + col] }, false, TileType::Square);
			if (Texel(tile, xc, yIn, color))
				return true;
		}
	}
	return false;
}

#endif // FP_TEXTURE_TEST

} // namespace devilution
