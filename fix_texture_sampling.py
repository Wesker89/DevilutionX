with open('Source/engine/render/fp_render.cpp', 'r') as f:
    content = f.read()

# Find and replace the SampleDungeonTexture function
old_start = 'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v) {'
new_start = 'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v, bool isWall = false) {'

content = content.replace(old_start, new_start)

# Now we need to replace the function body for LeftTrapezoid and RightTrapezoid cases
# Let's do a more targeted replacement using regex
import re

# First, update the function signature
content = content.replace(
    'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v) {',
    'uint8_t SampleDungeonTexture(const uint8_t *frameData, TileType tileType, float u, float v, bool isWall = false) {'
)

# Now we need to replace the LeftTrapezoid and RightTrapezoid cases
# This is complex, let's use a more targeted approach with sed
print("Using sed for targeted replacements...")

with open('Source/engine/render/fp_render.cpp', 'w') as f:
    f.write(content)

print("Step 1 done - updated function signature")