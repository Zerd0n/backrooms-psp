#!/usr/bin/env python3
"""Fail-fast release checks for the generated Backrooms PSP package."""

from __future__ import annotations

from collections import deque
from pathlib import Path
import re
import sys
import wave

from convert_textures import read_png, rgb565


REQUIRED_HEADERS = (
    "texture_wall.h", "texture_dirty_wall.h", "texture_carpet.h",
    "texture_ceiling.h", "texture_light.h", "texture_door.h",
    "pools_wall.h", "pools_damp_wall.h", "pools_floor.h",
    "pools_water.h", "pools_ceiling.h", "pools_light.h",
    "pools_edge.h", "nextbot_sprite.h", "level_maps.h",
)


def parse_define(text: str, name: str) -> int:
    match = re.search(rf"^#define {re.escape(name)} (\d+)$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing numeric define {name}")
    return int(match.group(1))


def parse_map(text: str, symbol: str, height: int) -> list[str]:
    match = re.search(rf"{symbol}\[[^\n]+\] = \{{(.*?)\n\}};", text, re.DOTALL)
    if not match:
        raise ValueError(f"cannot parse {symbol}")
    rows = re.findall(r'"([#\.DWE]+)"', match.group(1))
    if len(rows) != height:
        raise ValueError(f"{symbol}: expected {height} rows, got {len(rows)}")
    return rows


def distances(grid: list[str], start: tuple[int, int]) -> list[list[int]]:
    width = len(grid[0])
    result = [[-1] * width for _ in grid]
    queue = deque([start])
    result[start[1]][start[0]] = 0
    while queue:
        x, z = queue.popleft()
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, nz = x + dx, z + dz
            if 0 <= nz < len(grid) and 0 <= nx < width and result[nz][nx] < 0 and grid[nz][nx] in ".WE":
                result[nz][nx] = result[z][x] + 1
                queue.append((nx, nz))
    return result


def check(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)
    print(f"[PASS] {label}")


def main() -> int:
    root = Path.cwd()
    eboot = root / "EBOOT.PBP"
    check(eboot.is_file() and eboot.stat().st_size > 0, "EBOOT.PBP exists and is non-empty")
    generated = root / "src/generated"
    check(all((generated / name).is_file() for name in REQUIRED_HEADERS), "all generated headers exist")
    mip_headers = [name for name in REQUIRED_HEADERS if name not in ("nextbot_sprite.h", "level_maps.h")]
    check(all(all(f"_MIP{level}_PIXELS" in (generated / name).read_text(encoding="ascii") for level in (1, 2, 3))
              for name in mip_headers), "all world textures contain 64/32/16px mip levels")
    chase = root / "assets/chase.raw"
    ambient_level0 = root / "assets/ambient_level0.raw"
    ambient_poolrooms = root / "assets/ambient_poolrooms.raw"
    check(chase.is_file() and chase.stat().st_size > 0 and chase.stat().st_size % 2 == 0, "chase.raw is valid non-empty PCM16 data")
    check(ambient_level0.is_file() and ambient_level0.stat().st_size == 11025 * 8 * 2,
          "Level 0 ambient is an 8-second PCM16 loop")
    check(ambient_poolrooms.is_file() and ambient_poolrooms.stat().st_size == 11025 * 8 * 2,
          "Poolrooms ambient is an 8-second PCM16 loop")
    maps_text = (generated / "level_maps.h").read_text(encoding="ascii")
    level0 = parse_map(maps_text, "LEVEL0_MAP", 50)
    pool = parse_map(maps_text, "POOL_MAP", 32)
    level_distance = distances(level0, (5, 5))
    reachable = sum(value >= 0 for row in level_distance for value in row)
    walkable = sum(cell in ".WE" for row in level0 for cell in row)
    check(level0[5][5] == "." and reachable == walkable, "Level 0 spawn valid and every walkable cell connected")
    door_x = parse_define(maps_text, "LEVEL0_DOOR_X")
    door_z = parse_define(maps_text, "LEVEL0_DOOR_Z")
    approaches = [level_distance[door_z + dz][door_x + dx] for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)) if level_distance[door_z + dz][door_x + dx] >= 0]
    door_distance = min(approaches) if approaches else -1
    check(level0[door_z][door_x] == "D" and 60 <= door_distance <= 100, f"door reachable at ({door_x},{door_z}), BFS distance={door_distance}")
    initial_bot = parse_define(maps_text, "NEXTBOT_INITIAL_DISTANCE")
    check(20 <= initial_bot <= 36, f"nextbot initial BFS distance={initial_bot}")
    pool_distance = distances(pool, (2, 2))
    pool_reachable = sum(value >= 0 for row in pool_distance for value in row)
    pool_walkable = sum(cell in ".WE" for row in pool for cell in row)
    check(pool[2][2] == "." and pool_reachable == pool_walkable, "Poolrooms spawn valid and every walkable cell connected")
    pool_bot_distances = [value for row in pool_distance for value in row if 20 <= value <= 36]
    check(any(value == 28 for value in pool_bot_distances), "Poolrooms has a reachable nextbot spawn at BFS distance 28")
    main_source = (root / "src/main.c").read_text(encoding="utf-8")
    check("pspDebugScreenSetOffset" not in main_source, "no gameplay framebuffer debug-screen attachment")
    check("crosshair" not in main_source.lower(), "no crosshair implementation")
    check("g_zbuffer[SCREEN_W]" in main_source and "#define WORLD_W SCREEN_W" in main_source and
          "#define WORLD_H 136" in main_source and "#define BACKGROUND_W 240" in main_source and
          "INTERNAL_W" not in main_source, "PSP-optimized 480-column wall rendering enabled")
    check("fog_amount(perpendicular)" in main_source and "texture_lod(perpendicular)" in main_source,
          "distance fog and wall mip selection enabled")
    check("compute_distances(g_level" in main_source and "if (!g_nextbot.enabled || g_game_over)" in main_source and
          "if (!g_nextbot.enabled)" in main_source, "nextbot pathfinding and rendering support both levels")
    check("0xFF201B12U" in main_source and ", 96);" in main_source and ", 112);" in main_source and
          ", 100);" in main_source, "Poolrooms use strongly reduced lighting and near-black fog")
    check("((red >> 3) << 11)" in (root / "tools/convert_textures.py").read_text(encoding="utf-8"), "RGB565 red channel occupies high bits")
    sprite_width, sprite_height, sprite_pixels = read_png(root / "source_assets/nextbot.png")
    sprite_header = (generated / "nextbot_sprite.h").read_text(encoding="ascii")
    packed_match = re.search(r"NEXTBOT_SPRITE_PIXELS\[\d+\].*?= \{(.*?)\n\};", sprite_header, re.DOTALL)
    alpha_match = re.search(r"NEXTBOT_SPRITE_ALPHA\[\d+\].*?= \{(.*?)\n\};", sprite_header, re.DOTALL)
    if packed_match is None or alpha_match is None:
        raise ValueError("cannot parse generated nextbot arrays")
    packed = [int(value, 16) for value in re.findall(r"0x([0-9A-F]{4})", packed_match.group(1))]
    alpha = [int(value) for value in re.findall(r"\b\d+\b", alpha_match.group(1))]
    expected_packed = [rgb565(red, green, blue) for red, green, blue, _ in sprite_pixels]
    expected_alpha = [value[3] for value in sprite_pixels]
    check(sprite_width == 128 and sprite_height == 128 and packed == expected_packed and alpha == expected_alpha,
          "nextbot RGB565 and alpha exactly match supplied PNG (no R/B swap)")
    with wave.open(str(root / "source_assets/GAZ.wav"), "rb") as chase_source:
        check(chase_source.getnchannels() == 1 and chase_source.getsampwidth() == 2 and chase_source.getframerate() == 11025,
              "source chase audio is mono PCM16 at 11025 Hz")
        check(chase.stat().st_size == chase_source.getnframes() * 2,
              "converted chase.raw contains every source PCM sample")
    for ambient_name in ("ambient_level0", "ambient_poolrooms"):
        with wave.open(str(root / f"source_assets/{ambient_name}.wav"), "rb") as ambient_source:
            check(ambient_source.getnchannels() == 1 and ambient_source.getsampwidth() == 2 and
                  ambient_source.getframerate() == 11025 and ambient_source.getnframes() == 11025 * 8,
                  f"{ambient_name} source is mono PCM16 at 11025 Hz")
    check("No audio assets available; game will continue silently" in main_source and "if (!load_audio_assets())" in main_source,
          "missing audio assets follow non-fatal silent fallback")
    package = root / "dist/BACKROOMS3D"
    package.mkdir(parents=True, exist_ok=True)
    (package / "assets").mkdir(exist_ok=True)
    (package / "EBOOT.PBP").write_bytes(eboot.read_bytes())
    (package / "assets/chase.raw").write_bytes(chase.read_bytes())
    (package / "assets/ambient_level0.raw").write_bytes(ambient_level0.read_bytes())
    (package / "assets/ambient_poolrooms.raw").write_bytes(ambient_poolrooms.read_bytes())
    print(f"Release package ready: {package}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
