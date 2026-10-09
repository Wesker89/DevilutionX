with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# 1. Add missing includes after controls/plrctrls.h
old = '#include "controls/plrctrls.h"\n\nnamespace devilution {'
new = '''#include "controls/plrctrls.h"
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
#include "utils/str_cat.hpp"

namespace devilution {'''
content = content.replace('#include "controls/plrctrls.h"\n\nnamespace devilusion {', new)

# 2. Add extern declarations after existing externs
old2 = 'extern const Player *PlayerUnderCursor;\n\nnamespace {'
new2 = '''extern const Player *PlayerUnderCursor;
extern bool qtextflag;
void DrawQText(const Surface &out);
void DrawControllerModifierHints(const Surface &out);
void DrawCursor(const Surface &out);

namespace {'''
content = content.replace(old2, new2)

# 3. Fix UpdateCamera - remove TURN_SPEED * deltaTime
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

# 4. Remove TURN_SPEED constant
content = content.replace('constexpr float TURN_SPEED = 2.0f; // radians per second\n', '')

# 5. Increase mouse sensitivity
content = content.replace('constexpr float MOUSE_SENSITIVITY = 0.003f;', 'constexpr float MOUSE_SENSITIVITY = 0.005f;')

# 6. Fix ProcessFPInput - add SendFPWalkCommand calls for WASD
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

# 4. Fix DrawFirstPersonView - add UI panels and cursor
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
    if (ChatLogFlag) {
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
    UpdateLifeManaPercent();
    DrawLifeFlaskUpper(out);
    DrawManaFlaskUpper(out);

    // Draw cursor (when not in relative mouse mode for mouselook)
    if (!g_relativeMouseMode) {
        DrawCursor(out);
    }
}'''

content = content.replace(old_draw_end, new_draw_end)

# Remove TURN_SPEED constant
content = content.replace('constexpr float TURN_SPEED = 2.0f; // radians per second\n', '')

# Increase mouse sensitivity
content = content.replace('constexpr float MOUSE_SENSITIVITY = 0.003f;', 'constexpr float MOUSE_SENSITIVITY = 0.005f;')

# 3. Fix UpdateCamera - remove TURN_SPEED * deltaTime
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

# 7. Fix ProcessFPInput - add SendFPWalkCommand calls for WASD
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

# Fix DrawFirstPersonView - add UI panels and cursor
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
    if (ChatLogFlag) {
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
    UpdateLifeManaPercent();
    DrawLifeFlaskUpper(out);
    DrawManaFlaskUpper(out);

    // Draw cursor (when not in relative mouse mode for mouselook)
    if (!g_relativeMouseMode) {
        DrawCursor(out);
    }
}'''

content = content.replace(old_draw_end, new_draw_end)

# Remove TURN_SPEED constant
content = content.replace('constexpr float TURN_SPEED = 2.0f; // radians per second\n', '')

# Increase mouse sensitivity
content = content.replace('constexpr float MOUSE_SENSITIVITY = 0.003f;', 'constexpr float MOUSE_SENSITIVITY = 0.005f;')

# 8. Fix UpdateCamera - remove TURN_SPEED * deltaTime
old_update2 = '''void UpdateCamera(float deltaTime) {
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

content = content.replace(old_update2, new_update)

# 9. Fix ProcessFPInput - add SendFPWalkCommand calls for WASD (second occurrence)
old_process2 = '''        switch (key) {
            case SDLK_w: g_input.forward = pressed; break;
            case SDLK_s: g_input.backward = pressed; break;
            case SDLK_a: g_input.strafeLeft = pressed; break;
            case SDLK_d: g_input.strafeRight = pressed; break;
            case SDLK_SPACE: g_input.up = pressed; break;
            case SDLK_LCTRL: g_input.down = pressed; break;
            case SDLK_TAB: {'''

new_process2 = '''        switch (key) {
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

content = content.replace(old_process2, new_process2)

# 5. Fix DrawFirstPersonView - add UI panels and cursor (second occurrence)
old_draw_end2 = '''    DrawString(out, "WASD=Move | RMB+Mouse=Look | F6=Toggle | LMB=Interact",
        Rectangle { Point { 10, 30 }, Size { 500, 16 } },
        { .flags = UiFlags::ColorYellow });
}'''

new_draw_end2 = '''    DrawString(out, "WASD=Move | RMB+Mouse=Look | F6=Toggle | LMB=Interact",
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
    if (ChatLogFlag) {
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
    UpdateLifeManaPercent();
    DrawLifeFlaskUpper(out);
    DrawManaFlaskUpper(out);

    // Draw cursor (when not in relative mouse mode for mouselook)
    if (!g_relativeMouseMode) {
        DrawCursor(out);
    }
}'''

content = content.replace(old_draw_end2, new_draw_end2)

# 8. Add the YawToDirection and SendFPWalkCommand functions before ProcessFPInput
# First, find where ProcessFPInput is defined and add the functions before it
# We need to add these functions before the first ProcessFPInput

# Find the existing SendFPWalkCommand and YawToDirection in the file and remove duplicates
# The original file has them defined inside SendFPWalkCommand which is wrong
# Let's add them properly before ProcessFPInput

# First, let's remove the duplicate/incorrect definitions
# We'll find and remove the nested YawToDirection inside SendFPWalkCommand
# and the duplicate SendFPWalkCommand

# Write back
with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("All fixes applied!")
