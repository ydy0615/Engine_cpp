# AGENTS.md

## Build Commands

```bash
# Game demo
g++ test.cpp -o test -O2 -lpng -lX11 $(pkg-config --cflags --libs freetype2)

# Map editor
g++ map_editor.cpp -o map_editor -O2 -lpng -lX11 $(pkg-config --cflags --libs freetype2)
```

Run with `./test` or `./map_editor`.

## Dependencies

- libpng (`-lpng`)
- X11 (`-lX11`)
- FreeType2 (via `pkg-config --cflags --libs freetype2`)

Requires a display server (X11). No headless rendering.

## Architecture

- `test.cpp` - Game engine demo: loads map from JSON, renders with X11, WASD+QE controls
- `map_editor.cpp` - Tile-based map editor: 1694 lines, RGB brush, item placement, undo/redo, JSON import/export
- `json.hpp` - nlohmann/json header-only library

## Asset Conventions

- Sprite frames: `{name}/1.png`, `{name}/2.png`, etc. (numeric sequential)
- `2D_pixel/` - Contains third-party asset packs (read-only reference)
- `main_character/` - Character animation frames (4 frames)
- `items/` - Placeable sprite items with subdirectories

## Map Editor Specifics

- Item path priority: CLI arg > `MAP_EDITOR_ITEMS` env var > `./items`
- Exports to `map_export_{timestamp}.json` and `map_editor_last.json`
- `Color::is_empty()` treats RGB(255,255,255) as transparent
- Item templates auto-discovered: directories containing `1.png`

## Missing/Incomplete

- No tests, no lint, no CI
- `Effect` and `effect_system` classes in `test.cpp` are stubs (missing semicolons, incomplete)
- `test.cpp:450-455` - compilation errors if `Effect` struct is used
