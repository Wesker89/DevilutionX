with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Fix 1: Move YawToDirection outside of SendFPWalkCommand
# Find the problematic pattern
old_send = '''void SendFPWalkCommand(Direction dir) {

/**
 * Convert camera yaw to the nearest game direction.
 */
Direction YawToDirection(float yaw) {
    while (yaw < 0) yaw += 2.0f * M_PI;
    while (yaw >= 2.0f * M_PI) yaw -= 2.0f * M_PI;

    const float sector = M_PI / 4.0f;
    int sectorIdx = static_cast<int>((yaw + sector / 2.0f) / sector) % 8;

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

    if (MyPlayer == nullptr)
        return;'''

new_send = '''/**
 * Convert camera yaw to the nearest game direction.
 */
Direction YawToDirection(float yaw) {
    while (yaw < 0) yaw += 2.0f * M_PI;
    while (yaw >= 2.0f * M_PI) yaw -= 2.0f * M_PI;

    const float sector = M_PI / 4.0f;
    int sectorIdx = static_cast<int>((yaw + sector / 2.0f) / sector) % 8;

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

void SendFPWalkCommand(Direction dir) {
    if (MyPlayer == nullptr)
        return;'''

content = content.replace(old_send, new_send)

# Fix 2: Invert pitch in UpdateCamera
old_pitch = '''g_camera.pitch = std::clamp(g_camera.pitch + g_input.mouseDeltaY * TURN_SPEED * deltaTime, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);'''
new_pitch = '''g_camera.pitch = std::clamp(g_camera.pitch - g_input.mouseDeltaY * TURN_SPEED * deltaTime, -static_cast<float>(M_PI) / 2.5f, static_cast<float>(M_PI) / 2.5f);'''

content = content.replace(old_pitch, new_pitch)

# Write back
with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("Fixes applied!")
