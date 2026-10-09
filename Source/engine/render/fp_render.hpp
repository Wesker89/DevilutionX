/**
 * @file fp_render.hpp
 *
 * Interface for first-person rendering mode.
 */
#pragma once

#include "engine/point.hpp"
#include "engine/surface.hpp"

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

namespace devilution {

/**
 * @brief Render the first-person view.
 * @param out Target surface to render to
 * @param startPosition Center tile position (same as ViewPosition in isometric mode)
 */
void DrawFirstPersonView(const Surface &out, Point startPosition);

/**
 * @brief Handle input events for first-person mode.
 * @param event SDL event to process
 */
void HandleFirstPersonInput(const SDL_Event &event);

/**
 * @brief Discard accumulated mouse-look deltas.
 *
 * Called when leaving FPS mode so stale motion does not rotate
 * the camera on re-entry.
 */
void ClearFirstPersonMouseDeltas();

/**
 * @brief Toggle the geometry-only diagnostic view (flat untextured materials).
 *
 * Developer-only, off by default. Bound to F4.
 */
void ToggleFirstPersonGeometryDebug();

/**
 * @brief Reconcile the FPS camera with the authoritative player position.
 *
 * Called when entering FPS mode (and when the level changes underneath
 * it) so the camera never starts embedded in solid geometry.
 * Orientation is preserved; only the position is corrected.
 */
void OnEnterFirstPersonMode();

} // namespace devilution