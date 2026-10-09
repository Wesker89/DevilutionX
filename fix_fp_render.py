import re

with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Fix 1: Fix the SendFPWalkCommand function - it has YawToDirection embedded inside it
# Find the malformed SendFPWalkCommand and fix it
pattern1 = r'void SendFPWalkCommand\(Direction dir\) \{\s*\n\s*/\*\*\n \* Convert camera yaw to the nearest game direction\.\n \*/\nDirection YawToDirection\(float yaw\) \{\n.*?\}\n\n\s*if \(MyPlayer == nullptr\)'
replacement1 = '''void SendFPWalkCommand(Direction dir) {
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
}'''

content = re.sub(pattern1, replacement1, content, flags=re.DOTALL)

# Fix 2: Add YawToDirection function before SendFPWalkCommand
pattern2 = r'void SendFPWalkCommand\(Direction dir\) \{'
replacement2 = '''/**
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

void SendFPWalkCommand(Direction dir) {'''

content = re.sub(pattern2, replacement2, content, count=1)

# Fix 3: Modify ProcessFPInput to call SendFPWalkCommand for movement keys
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
