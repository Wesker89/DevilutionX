# DevilutionX First-Person Mode - Repository Assessment

**Date:** 2026-10-08
**Repository:** https://github.com/Wesker89/DevilutionX (fork)
**Branch:** Current working directory

---

## 1. Engine Architecture Overview

### Core Structure
DevilutionX is a faithful recreation of the original Diablo engine, written in modern C++ (C++23). The architecture separates:

- **Game Simulation** (`Source/*.cpp`): Player, monsters, items, spells, quests, dungeon generation, combat logic
- **Rendering Engine** (`Source/engine/render/`): Isometric tile/sprite rendering, lighting, text
- **Input System** (`Source/controls/`): Keyboard, mouse, gamepad via keymapper/padmapper
- **Platform Abstraction** (`Source/platform/`, `Source/utils/`): SDL2/SDL3, audio, file I/O, threading
- **UI System** (`Source/DiabloUI/`, `Source/panels/`): Menus, inventory, character, quest log

### Key Design Principles
- **Single-threaded game loop** with fixed tick rate (default 20 Hz)
- **Tile-based world** (MAXDUNX × MAXDUNY, typically 112×112)
- **Isometric projection** with 64×32 pixel tiles (TILE_WIDTH=64, TILE_HEIGHT=32)
- **Deterministic simulation** - rendering is decoupled from game logic
- **Data-driven** - dungeon tiles, monsters, items loaded from MPQ/assets

---

## 2. Relevant Source Files & Functions

### Rendering & Dungeon Drawing
| File | Purpose |
|------|---------|
| `Source/engine/render/scrollrt.cpp` | **Main rendering pipeline**: `DrawView()`, `DrawGame()`, `DrawTileContent()`, `DrawDungeon()`, `DrawCell()`, `DrawFloor()`, `DrawOOB()` |
| `Source/engine/render/dun_render.hpp/cpp` | Low-level tile rendering: `RenderTile()`, `RenderTileFrame()`, `TileType` enum, `MaskType` enum |
| `Source/engine/render/clx_render.hpp/cpp` | Sprite (CLX) rendering with lighting/TRN: `ClxDrawLight()`, `ClxDrawBlended()` |
| `Source/engine/render/light_render.hpp/cpp` | Per-pixel lighting, lightmaps, light tables |
| `Source/levels/dun_tile.hpp` | Tile definitions: `TileType`, `LevelCelBlock`, `TileProperties` (Solid, BlockLight, BlockMissile, Transparent) |
| `Source/levels/tile_properties.hpp/cpp` | Collision queries: `IsTileSolid()`, `IsTileWalkable()`, `CanStep()` |
| `Source/engine/world_tile.hpp` | Coordinate types: `WorldTilePosition` (uint8_t x,y) |

### Player Position, Movement & Collision
| File | Purpose |
|------|---------|
| `Source/player.cpp` | `PosOkPlayer()` (line 3092), `MakePlrPath()`, `SyncInitPlrPos()` |
| `Source/engine/actor_position.hpp/cpp` | `ActorPosition` struct: `tile`, `future`, `last`, `old`, `temp` |
| `Source/engine/path.h/cpp` | A* pathfinding: `FindPath()` |
| `Source/controls/plrctrls.cpp` | Input → actions: `FindActor()`, `Interact()`, `GetMoveDirection()` |

### Monster Positioning, AI & Animation
| File | Purpose |
|------|---------|
| `Source/monster.cpp` | `Monster` struct, AI modes (`MonsterMode`), `animInfo` |
| `Source/engine/render/scrollrt.cpp` | `DrawMonster()`, `DrawMonsterHelper()` (lines 378-402, 733-772) |

### Combat, Spells, Missiles
| File | Purpose |
|------|---------|
| `Source/missiles.cpp` | Missile logic, `UpdateMissileRendererData()`, `DrawMissile()` |
| `Source/spells.cpp` | Spell data, `CheckSpell()`, spell casting logic |
| `Source/controls/plrctrls.cpp` | `CheckPlrSpell()`, `Interact()` (melee/ranged attack logic) |

### Items, Objects, Doors, Interactions
| File | Purpose |
|------|---------|
| `Source/objects.cpp` | `Object` struct, door/chest/shrine logic |
| `Source/items.cpp` | Item logic, `InvList`, ground items (`dItem[][]`) |
| `Source/controls/plrctrls.cpp` | `FindItemOrObject()`, `Interact()` |

### Input Handling & Configurable Controls
| File | Purpose |
|------|---------|
| `Source/options.h` | `KeymapperOptions`, `PadmapperOptions`, `GraphicsOptions` |
| `Source/diablo.cpp` | `InitKeymapActions()` (line 1840), `InitPadmapActions()` (line 2160) |
| `Source/controls/keymapper.cpp` | `KeymapperPress()`, `KeymapperRelease()` |
| `Source/controls/game_controls.cpp` | `HandleControllerButtonEvent()`, `GetGameAction()` |

### Game Interface & HUD
| File | Purpose |
|------|---------|
| `Source/engine/render/scrollrt.cpp` | `DrawView()` (line 1313) - draws world + all UI panels |
| `Source/panels/*.cpp` | Individual panels: charpanel, quest_log, spell_list, mainpanel |
| `Source/inv.cpp` | Inventory rendering |

### Game Initialization, Save/Load, Config
| File | Purpose |
|------|---------|
| `Source/diablo.cpp` | `init()`, `FreeGameMem()`, game loop |
| `Source/loadsave.cpp` | Save/load: `SaveGame()`, `LoadGame()`, `ViewPosition` persisted |
| `Source/init.cpp` | Engine initialization, asset loading |
| `Source/options.cpp` | INI file load/save, option categories |

---

## 3. First-Person Renderer Implementation Recommendation

### Recommended Approach: New Rendering Backend (Not Replacement)

**Do not modify `scrollrt.cpp` directly.** Instead:

1. **Add a rendering mode enum** in `options.h`:
   ```cpp
   enum class RenderMode : uint8_t { Isometric, FirstPerson };
   OptionEntryEnum<RenderMode> renderMode;
   ```

2. **Create new renderer module**:
   - `Source/engine/render/fp_render.hpp/cpp` - First-person rendering implementation
   - `Source/engine/render/fp_dungeon.hpp/cpp` - Dungeon geometry conversion
   - `Source/engine/render/fp_sprites.hpp/cpp` - Sprite billboarding in 3D

3. **Modify `DrawView()` in `scrollrt.cpp`** to dispatch:
   ```cpp
   void DrawView(const Surface &out, Point startPosition) {
       if (*GetOptions().Graphics.renderMode == RenderMode::FirstPerson) {
           DrawFirstPersonView(out, startPosition);
       } else {
           // Existing isometric code
       }
   }
   ```

4. **Shared data access**: First-person renderer reads the **same** dungeon arrays:
   - `dPiece[][]` - dungeon piece IDs
   - `dSpecial[][]` - special objects (doors, arches)
   - `dMonster[][]`, `dPlayer[][]`, `dItem[][]`, `dObject[][]` - entity positions
   - `pDungeonCels` - tile graphics
   - `LightTables[]`, `dLight[][]` - lighting

### Why This Approach?
- **Zero risk to existing isometric renderer** - completely separate code path
- **Same game simulation** - `ViewPosition`, player position, monster positions unchanged
- **Clean separation** - first-person code isolated, easy to disable/debug
- **Follows existing pattern** - like `HeadlessMode` which skips rendering entirely

---

## 4. Sharing Game Simulation Between Views

Both views **already share the same simulation** because:

| State | Location | Shared? |
|-------|----------|---------|
| Player position/health/mana/stats | `Player` struct, `MyPlayer` global | ✅ Yes |
| Monster positions/AI/health | `Monsters[]`, `ActiveMonsters[]` | ✅ Yes |
| Dungeon layout (`dPiece`, `dSpecial`) | Global arrays in `gendung.h` | ✅ Yes |
| Items on ground | `dItem[][]`, `Items[]` | ✅ Yes |
| Objects/doors | `Objects[]`, `dObject[][]` | ✅ Yes |
| Quests, dungeon level, seed | `Quests[]`, `currlevel`, `gnDifficulty` | ✅ Yes |
| Inventory, equipment | `MyPlayer->InvList[]`, `InvGrid[]` | ✅ Yes |
| Save/load | `loadsave.cpp` serializes all above | ✅ Yes |

**Only rendering differs.** The camera in isometric mode is derived from `ViewPosition` (tile coordinates). First-person mode would use:
- **Camera position**: `ViewPosition` converted to world units (1 tile = 1 unit)
- **Camera yaw/pitch**: New state variables (persisted in save if desired)
- **Targeting**: Raycast from camera through screen center → tile coordinates

---

## 5. Main Technical Challenges

### A. Dungeon Representation → First-Person Geometry
**Challenge**: Diablo dungeons are **2.5D tile-based** (isometric), not true 3D.
- Tiles are 64×32 pixel rhombuses representing ~1×1 meter squares
- Walls are **implied** by `TileProperties::Solid` + `TileType::Left/RightTrapezoid` 
- No explicit wall geometry, height maps, or ceiling data exists

**Approach**: Reconstruct 3D geometry from tile properties:
```
For each tile (x,y):
  if TileHasAny(Solid):
    - Floor quad at y=0
    - Wall quads on 4 sides where neighbor is NOT Solid
    - Ceiling quad at y=CEILING_HEIGHT (constant or per-tileset)
  else:
    - Floor quad only
```
- Use existing `TileType` to determine wall shape (trapezoid → vertical wall)
- Tileset (Catacombs/Caves/Hell) determines wall textures via existing `pDungeonCels`

### B. Sprite Rendering in 3D (Billboards)
**Challenge**: Monsters, players, items are 2D sprites (CLX frames, 8 directions)
- Must billboard toward camera
- Must scale with distance (perspective)
- Must handle lighting (existing light tables/TRN)
- Must sort by depth (painter's algorithm or z-buffer)

**Approach**: Reuse `ClxSprite` and `ClxDrawLight()` logic:
- Project sprite center to screen space
- Scale based on distance from camera
- Apply existing lighting via `LightTables[lightTableIndex]`
- Sort entities by camera distance (back-to-front)

### C. Camera Controls & Movement Mapping
**Challenge**: Isometric movement is 8-directional tile-based; FPS expects continuous 3D movement
- Original: Click-to-move or 8-dir keyboard → A* pathfind → tile-by-tile
- FPS: WASD + mouse look → continuous velocity → collision check per frame

**Approach**: 
- **Keep existing movement logic** - player still moves tile-to-tile via `PosOkPlayer()`/`CanStep()`
- **Map FPS input to existing actions**: 
  - W/S → `NetSendCmdLoc()` with direction derived from camera yaw
  - Mouse look → updates camera yaw/pitch only (not player `_pdir` directly)
  - Click-to-move still works in FPS mode
- **Camera follows player** with smoothing, offset by player animation

### D. Targeting & Interaction
**Challenge**: Original uses mouse cursor in screen space → `cursPosition` (tile coords)
- FPS: Crosshair at screen center → raycast → tile/entity under crosshair

**Approach**:
- Raycast from camera through screen center
- Find first intersected tile/entity
- Set `cursPosition`, `pcursmonst`, `pcursitem`, `ObjectUnderCursor` same as isometric
- Existing `Interact()`, `CheckPlrSpell()` work unchanged

### E. Lighting
**Challenge**: Original uses precomputed light tables + per-pixel lightmaps
- Light radius falls off in diamond pattern (Manhattan distance)
- Per-pixel lighting blends lightmap with tile pixels

**Approach**:
- Reuse `dLight[][]` (light radius per tile) and `LightTables[]`
- For walls: compute light per-vertex or per-fragment using same falloff
- For sprites: reuse existing `ClxDrawLight()` with light table index

---

## 6. Implementation Plan (Lowest → Highest Risk)

| Milestone | Task | Risk | Verification |
|-----------|------|------|--------------|
| **M1: Baseline** | Build project, run game, verify isometric works | ✅ None | `./devilutionx` launches |
| **M1: Assessment** | Document all findings (this document) | ✅ None | This document |
| **M2: Toggle** | Add `RenderMode` option + keybind (F6) + dispatch in `DrawView()` | 🟢 Low | Press F6 → no crash, isometric still works |
| **M2: Stub FP** | Add `DrawFirstPersonView()` stub that clears screen + draws "FP MODE" text | 🟢 Low | F6 toggles between views |
| **M3: Camera** | Implement camera: position from `ViewPosition`, yaw/pitch from mouse, movement input mapping | 🟡 Medium | Move with WASD, look with mouse, player follows |
| **M3: Floor/Ceiling** | Render floor/ceiling quads for visible tiles (no walls yet) | 🟡 Medium | See floor grid matching dungeon layout |
| **M3: Walls** | Generate wall quads from `TileProperties::Solid` + neighbor checks | 🟡 Medium | Corridors/rooms visible with correct walls |
| **M3: Lighting** | Apply `dLight`/`LightTables` to floor/wall geometry | 🟡 Medium | Light radius matches isometric |
| **M4: Sprites** | Billboard monsters/players/items using existing `ClxSprite` + lighting | 🟠 High | Entities appear at correct positions, animate |
| **M4: Targeting** | Crosshair raycast → sets `cursPosition`/`pcursmonst`/etc. | 🟠 High | Click attacks monster, opens door, picks item |
| **M4: Combat** | Verify melee/ranged/spells work identically to isometric | 🟠 High | Damage, hit chance, mana cost unchanged |
| **M5: Polish** | Sprite scaling, animation smoothing, FOV culling, HUD integration | 🔴 Higher | 60 FPS, no visual artifacts |
| **M5: Save/Load** | Persist camera yaw/pitch (optional), verify state identical on toggle | 🟢 Low | F6 → save → load → F6 works |
| **M5: Testing** | All dungeon types, quests, multiplayer, edge cases | 🔴 High | No regressions in isometric mode |

---

## 7. Linux Build Commands & Dependencies

### Verified from `CMakeLists.txt` and `vcpkg.json`

**Dependencies** (via vcpkg):
```json
{
  "dependencies": [
    "sdl2", "sdl2-image", "sdl2-mixer", "sdl2-ttf",
    "fmt", "spdlog", "tl-expected", "magic-enum",
    "sol2", "ankerl-unordered-dense", "mpqfs",
    "sodium", "asio", "zerotier", "speexdsp",
    "picosha2", "libsmackerdec", "audiolib"
  ]
}
```

**Build Commands** (standard CMake out-of-source):
```bash
# Clone & setup (already done)
cd /home/damashi/Desktop/DiabloFPS

# Install vcpkg dependencies (if not already)
# vcpkg install sdl2 sdl2-image sdl2-mixer sdl2-ttf fmt spdlog tl-expected magic-enum sol2 ankerl-unordered-dense mpqfs sodium asio zerotier speexdsp picosha2 libsmackerdec audiolib

# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Run
./build/devilutionx
```

**Verified Working**: The repository builds with standard CMake 3.22+. No custom build scripts needed.

---

## 8. Assumptions & Unresolved Questions

### Verified Facts ✅
- Engine uses fixed 20 Hz game tick, rendering interpolates between ticks
- `ViewPosition` (Point/WorldTilePosition) is the camera center in tile coords
- Dungeon tiles are 64×32 px rhombuses, stored as CEL frames in `pDungeonCels`
- Collision uses `TileProperties::Solid` + object `_oSolidFlag` + entity occupancy
- Input system is action-based (keymapper/padmapper), easily extensible
- Save/load serializes full game state including `ViewPosition`

### Assumptions ⚠️
- First-person camera height ≈ 1.6 tiles (player eye level) - needs tuning
- Wall height constant (e.g., 2.5 tiles) - original has no ceiling variation
- Sprite billboarding works with existing 8-direction frames - may need interpolation
- Existing light falloff (Manhattan distance) translates to Euclidean in 3D
- Mouse look sensitivity can reuse existing mouse motion handling

### Unresolved Questions ❓
1. **Ceiling geometry**: Original has no ceiling tiles. Use flat ceiling at fixed height? Per-tileset?
2. **Liquid rendering**: Water/lava tiles (catacombs) - need special shaders/transparency?
3. **Multiplayer sync**: Camera yaw/pitch not synced - only host needs it? Or sync for spectators?
4. **Performance**: Software renderer (Surface) vs. GPU - first-person needs 3D transforms. May need GPU path.
5. **FOV culling**: Current renderer draws ~20×15 tiles. First-person needs frustum culling for performance.
6. **Automap in FP mode**: Show mini-map? Full-screen map? 3D wireframe?
7. **Accessibility**: Screen reader integration - how to describe 3D view?

---

## Next Steps

**Awaiting approval to proceed with Milestone 2 (Rendering Mode Toggle):**
1. Add `RenderMode` enum + option in `options.h/cpp`
2. Add keymapper action "ToggleRenderMode" (default F6) in `diablo.cpp`
3. Add padmapper action for gamepad
4. Modify `DrawView()` in `scrollrt.cpp` to dispatch based on mode
5. Add stub `DrawFirstPersonView()` in new `fp_render.cpp`
6. Build & test toggle works without breaking isometric mode

**No source files will be modified until explicit approval.**