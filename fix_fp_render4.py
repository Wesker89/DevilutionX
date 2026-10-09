import re

with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Fix the SendFPWalkCommand function - it's missing its body
# Find the corrupted function definition and fix it
pattern = r'void SendFPWalkCommand\(Direction dir\) \{\s*\n\s*if \(MyPlayer == nullptr\)\s*\n\s*void ProcessFPInput\(const SDL_Event &event\) \{'
replacement = '''void SendFPWalkCommand(Direction dir) {
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

content = re.sub(r'void SendFPWalkCommand\(Direction dir\) \{\s*\n\s*if \(MyPlayer == nullptr\)\s*\n\s*void ProcessFPInput\(const SDL_Event &event\) \{', replacement, content)

with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("Done!")
