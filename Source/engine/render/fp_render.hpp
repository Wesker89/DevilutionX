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

} // namespace devilution