import re

with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# 1. Add missing includes after line 33 (after #include "controls/plrctrls.h")
old_includes = '''#include "controls/plrctrls.h"

namespace devilution {'''

new_includes = '''#include "controls/plrctrls.h"
#include "diablo.h"
#include "cursor.h"
#include "control/control.hpp"
#include "panels/charpanel.hpp"
#include "panels/quest_log.hpp"
#include "panels/spell_book.hpp"
#include "panels/spell_list.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "qol/chatlog.h"
#include "stores.h"
#include "towners.h"
#include "help.h"
#include "dead.h"
#include "gmenu.h"
#include "doom.h"
#include "inv.h"
#include "plrmsg.h"
#include "qol/chatlog.h"

namespace devilution {'''

content = content.replace(old_includes, new_includes)

# 2. Add forward declarations after the extern declarations
old_externs = '''extern const Player *PlayerUnderCursor;

namespace {'''

new_externs = '''extern const Player *PlayerUnderCursor;
extern bool qtextflag;
void DrawQText(const Surface &out);
void DrawControllerModifierHints(const Surface &out);
void DrawCursor(const Surface &out);

namespace {'''

content = content.replace(old_externs, new_externs)

# 3. Fix the ProcessFPInput to call SendFPWalkCommand
old_process = '''        switch (key) {
            case SDLK_w: g_input.forward = pressed; break;
            case SDLK_s: g_input.backward = pressed; break;
            case SDLK_a: g_input.strafeLeft = pressed; break;
            case SDLK_d: g_input.strafeRight = pressed; break;
            case SDLK_SPACE: g_input.up = pressed; break;
            case SDLK_LCTRL: g_input.down = pressed; break;
            case SDLK_TAB: {'''

new_process = '''        switch (key) {
            case SDLK_w:
                g_input.forward = pressed;
                if (pressed) SendFPWalkCommand(YawToDirection(g_camera.yaw));
                break;
            case SDLK_s:
                g_input.backward = pressed;
                if (pressed) SendFPWalkCommand(YawToDirection(g_camera.yaw + M_PI));
                break;
            case SDLK_a:
                g_input.strafeLeft = pressed;
                if (pressed) SendFPWalkCommand(YawToDirection(g_camera.yaw - M_PI / 2.0f));
                break;
            case SDLK_d:
                g_input.strafeRight = pressed;
                if (pressed) SendFPWalkCommand(YawToDirection(g_camera.yaw + M_PI / 2.0f));
                break;
            case SDLK_TAB: {'''

content = content.replace(old_process, new_process)

# 4. Add the SendFPWalkCommand and YawToDirection functions before ProcessFPInput
old_send = '''void ProcessFPInput(const SDL_Event &event) {'''

new_send = '''/**
 * Convert camera yaw to the nearest game direction.
 */
Direction YawToDirection(float yaw) {
    // Normalize yaw to [0, 2*PI)
    while (yaw < 0) yaw += 2.0f * M_PI;
    while (yaw >= 2.0f * M_PI) yaw -= 2.0f * M_PI;

    // Divide circle into 8 directions (45 degrees each)
    // North = 0 (or 2*PI), East = PI/2, South = PI, West = 3*PI/2
    const float sector = M_PI / 4.0f; // 45 degrees
    int sectorIdx = static_cast<int>((yaw + sector / 2.0f) / sector) % 8;

    // Map sector index to Direction enum
    // 0 = North, 1 = NorthEast, 2 = East, 3 = SouthEast, 4 = South, 5 = SouthWest, 6 = West, 7 = NorthWest
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

/**
 * Send a walk command for first-person movement.
 * Calculates the target tile based on camera direction and sends a walk command.
 */
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

void ProcessFPInput(const SDL_Event &event) {'''

content = content.replace('void ProcessFPInput(const SDL_Event &event) {', new_send)

# 5. Fix UpdateCamera to not multiply mouse delta by deltaTime
old_update = '''void UpdateCamera(float deltaTime) {
    // Rotation
    g_camera.yaw += g_input.mouseDeltaX * TURN_SPEED * deltaTime;
    g_camera.pitch = std::clamp(g_camera.pitch + g_input.mouseDeltaY * TURN_SPEED * deltaTime, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;'''

new_update = '''void UpdateCamera(float deltaTime) {
    // Rotation (mouse delta is already in radians, don't multiply by deltaTime)
    g_camera.yaw += g_input.mouseDeltaX;
    g_camera.pitch = std::clamp(g_camera.pitch + g_input.mouseDeltaY, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;'''

content = content.replace(old_update, new_update)

# 6. Increase mouse sensitivity
content = content.replace('constexpr float MOUSE_SENSITIVITY = 0.003f;', 'constexpr float MOUSE_SENSITIVITY = 0.005f;')

# 6. Fix the DrawFirstPersonView to add UI panels and cursor
old_draw_end = '''    DrawString(out, "WASD=Move | RMB+Mouse=Look | F6=Toggle | LMB=Interact",
        Rectangle { Point { 10, 30 }, Size { 500, 16 } },
        { .flags = UiFlags::ColorYellow });
}'''

new_draw_end = '''    DrawString(out, "WASD=Move | RMB+Mouse=Look | F6=Toggle | LMB=Interact",
        Rectangle { Point { 10, 30 }, Size { 500, 16 } },
        { .flags = UiFlags::ColorYellow });

    // Draw UI panels (inventory, character, spellbook, etc.) - same as isometric mode
    if (IsPlayerInStore() && !qtextflag)
        DrawSText(out);
    if (invflag) {
        DrawInv(out);
    } else if (SpellbookFlag) {
        DrawSpellBook(out);
    }

    DrawDurIcon(out);

    DrawLevelButton(out);

    if (CharFlag) {
        DrawChr(out);
    } else if (QuestLogIsOpen) {
        DrawQuestLog(out);
    } else if (IsStashOpen) {
        DrawStash(out);
    } else if (IsVisualStoreOpen) {
        DrawVisualStore(out);
    }

    if (ShowUniqueItemInfoBox) {
        DrawUniqueInfo(out);
    }
    if (qtextflag) {
        DrawQText(out);
    }
    if (SpellSelectFlag) {
        DrawSpellList(out);
    }
    if (DropGoldFlag) {
        DrawGoldSplit(out);
    }
    DrawGoldWithdraw(out);
    if (HelpFlag) {
        DrawHelp(out);
    }
    if (ChatFlag) {
        DrawChatLog(out);
    }
    if (MyPlayerIsDead) {
        RedBack(out);
        DrawDeathText(out);
    } else if (PauseMode != 0) {
        gmenu_draw_pause(out);
    }
    if (IsDiabloMsgAvailable()) {
        DrawDiabloMsg(out.subregionY(0, out.h() - GetMainPanel().size.height));
    }

    DrawControllerModifierHints(out);
    DrawPlrMsg(out);
    gmenu_draw(out);
    doom_draw(out);
    DrawInfoBox(out);
    UpdateLifeManaPercent(); // Update life/mana totals before rendering any portion of the flask.
    DrawLifeFlaskUpper(out);
    DrawManaFlaskUpper(out);

    // Draw cursor (when not in relative mouse mode for mouselook)
    if (!g_relativeMouseMode) {
        DrawCursor(out);
    }
}'''

content = content.replace(old_draw_end, new_draw_end)

# 5. Remove unused TURN_SPEED constant
content = content.replace('constexpr float TURN_SPEED = 2.0f; // radians per second\n', '')

# 6. Fix UpdateCamera to not multiply by deltaTime
old_update2 = '''void UpdateCamera(float deltaTime) {
    // Rotation
    g_camera.yaw += g_input.mouseDeltaX * TURN_SPEED * deltaTime;
    g_camera.pitch = std::clamp(g_camera.pitch + g_input.mouseDeltaY * TURN_SPEED * deltaTime, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;'''

new_update2 = '''void UpdateCamera(float deltaTime) {
    // Rotation (mouse delta is already in radians, don't multiply by deltaTime)
    g_camera.yaw += g_input.mouseDeltaX;
    g_camera.pitch = std::clamp(g_camera.pitch + g_input.mouseDeltaY, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);
    g_input.mouseDeltaX = 0.0f;
    g_input.mouseDeltaY = 0.0f;'''

content = content.replace(old_update2, new_update2)

# 6. Fix the mouse sensitivity
content = content.replace('constexpr float MOUSE_SENSITIVITY = 0.003f;', 'constexpr float MOUSE_SENSITIVITY = 0.005f;')

# 7. Fix the mouse sensitivity in ProcessFPInput
content = content.replace('constexpr float MOUSE_SENSITIVITY = 0.003f;', 'constexpr float MOUSE_SENSITIVITY = 0.005f;')

# Write the fixed content
with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("All fixes applied!")
