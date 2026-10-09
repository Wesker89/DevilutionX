import re

with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Fix 1: Remove duplicate SendFPWalkCommand function
# The second SendFPWalkCommand (lines 998-1018) is a duplicate
pattern_dup = r'\n\s+return;\s*\n\s*// Calculate target tile based on direction\n\s+Point target = MyPlayer->position\.tile;\n\s+switch \(dir\) \{\n\s+case Direction::North:    target\.y -= 1; break;\n\s+case Direction::South:    target\.y \+= 1; break;\n\s+case Direction::West:     target\.x -= 1; break;\n\s+case Direction::East:     target\.x \+= 1; break;\n\s+case Direction::NorthWest: target\.x -= 1; target\.y -= 1; break;\n\s+case Direction::NorthEast: target\.x \+= 1; target\.y -= 1; break;\n\s+case Direction::SouthWest: target\.x -= 1; target\.y \+= 1; break;\n\s+case Direction::SouthEast: target\.x \+= 1; target\.y \+= 1; break;\n\s+default: return;\n\s+\}\n\s*\n\s+// Check if target is valid\n\s+if \(InDungeonBounds\(target\) && PosOkPlayer\(\*MyPlayer, target\)\) \{\n\s+NetSendCmdLoc\(MyPlayerId, true, CMD_WALKXY, target\);\n\s+\}\n\}'
replacement_dup = ''
content = re.sub(pattern_dup, replacement_dup, content, flags=re.DOTALL)

# Also remove the extra "return;" line that was left
content = re.sub(r'\n\s+return;\s*\n\s+// Calculate target tile based on direction', '\n', content)

# Fix 2: Modify ProcessFPInput to call SendFPWalkCommand
# Find the switch statement in ProcessFPInput and add SendFPWalkCommand calls
pattern3 = r'(case SDLK_w: g_input\.forward = pressed; break;\n\s*case SDLK_s: g_input\.backward = pressed; break;\n\s*case SDLK_a: g_input\.strafeLeft = pressed; break;\n\s*case SDLK_d: g_input\.strafeRight = pressed; break;)'
replacement3 = '''case SDLK_w:
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
                break;'''

content = re.sub(pattern3, replacement3, content)

with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("Done!")
