with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

old_text = '''DrawString(out, "WASD=Move | RMB+Mouse=Look | F6=Toggle | LMB=Interact",
        Rectangle { Point { 10, 30 }, Size { 500, 16 } },
        { .flags = UiFlags::ColorYellow });
}'''

new_text = '''DrawString(out, "WASD=Move | RMB+Mouse=Look | F6=Toggle | LMB=Interact",
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
    UpdateLifeManaPercent(); // Update life/mana totals before rendering any portion of the flask.
    DrawLifeFlaskUpper(out);
    DrawManaFlaskUpper(out);

    // Draw cursor (when not in relative mouse mode for mouselook)
    if (!g_relativeMouseMode) {
        DrawCursor(out);
    }
}'''

content = content.replace(old_text, new_text)

with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("Done!")
