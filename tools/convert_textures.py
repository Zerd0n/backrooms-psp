#!/usr/bin/env python3
"""Convert source PNGs to PSP-friendly RGB565 C headers and menu PNGs.

Only the Python standard library is used so preprocessing is reproducible on a
clean macOS installation. Supported input PNGs are non-interlaced, 8-bit RGB
and RGBA, which is validated before any output is written.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
from pathlib import Path
import random
import struct
import sys
import zlib
from collections import deque


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
TEXTURE_SIZE = 128

TEXTURES = {
    "wallpaper.png": ("texture_wall.h", "TEXTURE_WALL", False),
    "dirty_wallpaper.png": ("texture_dirty_wall.h", "TEXTURE_DIRTY_WALL", False),
    "carpet.png": ("texture_carpet.h", "TEXTURE_CARPET", False),
    "drop ceiling.png": ("texture_ceiling.h", "TEXTURE_CEILING", False),
    "light panel.png": ("texture_light.h", "TEXTURE_LIGHT", False),
    "door.png": ("texture_door.h", "TEXTURE_DOOR", False),
    "Backrooms Pools wall.png": ("pools_wall.h", "POOLS_WALL", False),
    "damp Backrooms Pools wall.png": ("pools_damp_wall.h", "POOLS_DAMP_WALL", False),
    "wet tiled floor.png": ("pools_floor.h", "POOLS_FLOOR", False),
    "pool water.png": ("pools_water.h", "POOLS_WATER", False),
    "pool ceiling.png": ("pools_ceiling.h", "POOLS_CEILING", False),
    "ceiling light panel for Backrooms Pools.png": ("pools_light.h", "POOLS_LIGHT", False),
    "pool edge.png": ("pools_edge.h", "POOLS_EDGE", False),
    "nextbot.png": ("nextbot_sprite.h", "NEXTBOT_SPRITE", True),
}

SOFT_TEXTURE_SYMBOLS = {
    "TEXTURE_CARPET", "TEXTURE_CEILING", "TEXTURE_LIGHT",
    "POOLS_FLOOR", "POOLS_WATER", "POOLS_CEILING", "POOLS_LIGHT", "POOLS_EDGE",
}


class Logger:
    def __init__(self, root: Path) -> None:
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        log_dir = root / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        self.path = log_dir / f"preprocess_{stamp}.log"

    def info(self, message: str) -> None:
        line = f"[{dt.datetime.now().isoformat(timespec='seconds')}] {message}"
        print(line, flush=True)
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(line + "\n")


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path: Path) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path}: invalid PNG signature")

    position = len(PNG_SIGNATURE)
    width = height = color_type = bit_depth = interlace = None
    compressed = bytearray()
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        chunk_type = data[position + 4 : position + 8]
        chunk_data = data[position + 8 : position + 8 + length]
        expected_crc = struct.unpack_from(">I", data, position + 8 + length)[0]
        actual_crc = zlib.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            raise ValueError(f"{path}: CRC mismatch in {chunk_type!r}")
        position += length + 12
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
            if compression != 0 or filtering != 0:
                raise ValueError(f"{path}: unsupported PNG compression/filter method")
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if not width or not height:
        raise ValueError(f"{path}: missing/invalid IHDR")
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError(
            f"{path}: expected non-interlaced 8-bit RGB/RGBA PNG, "
            f"got depth={bit_depth}, type={color_type}, interlace={interlace}"
        )

    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    expected = height * (stride + 1)
    if len(raw) != expected:
        raise ValueError(f"{path}: decoded size {len(raw)} does not match {expected}")

    rows: list[bytearray] = []
    cursor = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        scan = bytearray(raw[cursor : cursor + stride])
        cursor += stride
        if filter_type == 1:
            for i in range(stride):
                scan[i] = (scan[i] + (scan[i - channels] if i >= channels else 0)) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                scan[i] = (scan[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = scan[i - channels] if i >= channels else 0
                scan[i] = (scan[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = scan[i - channels] if i >= channels else 0
                upper_left = previous[i - channels] if i >= channels else 0
                scan[i] = (scan[i] + _paeth(left, previous[i], upper_left)) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
        rows.append(scan)
        previous = scan

    pixels: list[tuple[int, int, int, int]] = []
    for row in rows:
        for x in range(width):
            base = x * channels
            alpha = row[base + 3] if channels == 4 else 255
            pixels.append((row[base], row[base + 1], row[base + 2], alpha))
    return width, height, pixels


def resize_cover(
    width: int,
    height: int,
    pixels: list[tuple[int, int, int, int]],
    target_width: int,
    target_height: int,
) -> list[tuple[int, int, int, int]]:
    source_aspect = width / height
    target_aspect = target_width / target_height
    if source_aspect > target_aspect:
        crop_h = float(height)
        crop_w = crop_h * target_aspect
        crop_x = (width - crop_w) * 0.5
        crop_y = 0.0
    else:
        crop_w = float(width)
        crop_h = crop_w / target_aspect
        crop_x = 0.0
        crop_y = (height - crop_h) * 0.5

    output: list[tuple[int, int, int, int]] = []
    for ty in range(target_height):
        sy = crop_y + ((ty + 0.5) * crop_h / target_height) - 0.5
        y0 = max(0, min(height - 1, int(sy)))
        y1 = min(height - 1, y0 + 1)
        fy = max(0.0, min(1.0, sy - y0))
        for tx in range(target_width):
            sx = crop_x + ((tx + 0.5) * crop_w / target_width) - 0.5
            x0 = max(0, min(width - 1, int(sx)))
            x1 = min(width - 1, x0 + 1)
            fx = max(0.0, min(1.0, sx - x0))
            p00 = pixels[y0 * width + x0]
            p10 = pixels[y0 * width + x1]
            p01 = pixels[y1 * width + x0]
            p11 = pixels[y1 * width + x1]
            channels = []
            for channel in range(4):
                top = p00[channel] * (1.0 - fx) + p10[channel] * fx
                bottom = p01[channel] * (1.0 - fx) + p11[channel] * fx
                channels.append(int(top * (1.0 - fy) + bottom * fy + 0.5))
            output.append(tuple(channels))  # type: ignore[arg-type]
    return output


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def downsample_2x(
    pixels: list[tuple[int, int, int, int]], size: int
) -> list[tuple[int, int, int, int]]:
    """Box-filter a square texture to its next mip level."""
    if size < 2 or size % 2 != 0 or len(pixels) != size * size:
        raise ValueError(f"invalid mip source: size={size}, pixels={len(pixels)}")
    target_size = size // 2
    output: list[tuple[int, int, int, int]] = []
    for y in range(target_size):
        for x in range(target_size):
            samples = (
                pixels[(y * 2) * size + x * 2],
                pixels[(y * 2) * size + x * 2 + 1],
                pixels[(y * 2 + 1) * size + x * 2],
                pixels[(y * 2 + 1) * size + x * 2 + 1],
            )
            output.append(tuple(sum(sample[channel] for sample in samples) // 4 for channel in range(4)))  # type: ignore[arg-type]
    return output


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def write_header(
    path: Path,
    symbol: str,
    pixels: list[tuple[int, int, int, int]],
    include_alpha: bool,
) -> None:
    guard = f"BACKROOMS_GENERATED_{symbol}_H"
    if symbol in SOFT_TEXTURE_SYMBOLS:
        half_size = TEXTURE_SIZE // 2
        pixels = resize_cover(
            half_size,
            half_size,
            downsample_2x(pixels, TEXTURE_SIZE),
            TEXTURE_SIZE,
            TEXTURE_SIZE,
        )
    packed = [rgb565(r, g, b) for r, g, b, _ in pixels]
    lines = [
        "/* Generated by tools/convert_textures.py. Do not edit manually. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "#include <stdint.h>",
        f"#define {symbol}_WIDTH {TEXTURE_SIZE}",
        f"#define {symbol}_HEIGHT {TEXTURE_SIZE}",
        f"static const uint16_t {symbol}_PIXELS[{len(packed)}] __attribute__((aligned(16))) = {{",
    ]
    for start in range(0, len(packed), 12):
        values = ", ".join(f"0x{value:04X}" for value in packed[start : start + 12])
        lines.append(f"    {values},")
    lines.append("};")
    if not include_alpha:
        mip_pixels = pixels
        mip_size = TEXTURE_SIZE
        for mip_level in range(1, 4):
            mip_pixels = downsample_2x(mip_pixels, mip_size)
            mip_size //= 2
            mip_packed = [rgb565(r, g, b) for r, g, b, _ in mip_pixels]
            lines.append(f"#define {symbol}_MIP{mip_level}_SIZE {mip_size}")
            lines.append(
                f"static const uint16_t {symbol}_MIP{mip_level}_PIXELS[{len(mip_packed)}] __attribute__((aligned(16))) = {{"
            )
            for start in range(0, len(mip_packed), 12):
                values = ", ".join(f"0x{value:04X}" for value in mip_packed[start : start + 12])
                lines.append(f"    {values},")
            lines.append("};")
    if include_alpha:
        alpha = [pixel[3] for pixel in pixels]
        lines.append(
            f"static const uint8_t {symbol}_ALPHA[{len(alpha)}] __attribute__((aligned(16))) = {{"
        )
        for start in range(0, len(alpha), 20):
            values = ", ".join(str(value) for value in alpha[start : start + 20])
            lines.append(f"    {values},")
        lines.append("};")
    lines.extend([f"#endif /* {guard} */", ""])
    atomic_write(path, "\n".join(lines).encode("ascii"))


def write_png(
    path: Path,
    width: int,
    height: int,
    pixels: list[tuple[int, int, int, int]],
) -> None:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for pixel in pixels[y * width : (y + 1) * width]:
            raw.extend(pixel)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    data = PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    atomic_write(path, data)


def bfs(grid: list[list[str]], start: tuple[int, int]) -> list[list[int]]:
    height, width = len(grid), len(grid[0])
    distance = [[-1] * width for _ in range(height)]
    sx, sz = start
    if grid[sz][sx] not in (".", "W", "E"):
        raise ValueError(f"invalid BFS start {start}")
    distance[sz][sx] = 0
    queue: deque[tuple[int, int]] = deque([(sx, sz)])
    while queue:
        x, z = queue.popleft()
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, nz = x + dx, z + dz
            if 0 <= nx < width and 0 <= nz < height and distance[nz][nx] < 0 and grid[nz][nx] in (".", "W", "E"):
                distance[nz][nx] = distance[z][x] + 1
                queue.append((nx, nz))
    return distance


def make_level0() -> tuple[list[list[str]], tuple[int, int], tuple[int, int], int, int]:
    width = height = 50
    grid = [["#"] * width for _ in range(height)]
    maze_start = (3, 1)
    start = (5, 5)
    rng = random.Random(0xBACC0A)
    odd_cells = [(x, z) for z in range(1, height - 1, 2) for x in range(1, width - 1, 2)]
    visited = {maze_start}
    stack = [maze_start]
    grid[maze_start[1]][maze_start[0]] = "."
    while stack:
        x, z = stack[-1]
        neighbors = [(x + dx, z + dz) for dx, dz in ((2, 0), (-2, 0), (0, 2), (0, -2)) if (x + dx, z + dz) in odd_cells and (x + dx, z + dz) not in visited]
        if not neighbors:
            stack.pop()
            continue
        nx, nz = rng.choice(neighbors)
        grid[(z + nz) // 2][(x + nx) // 2] = "."
        grid[nz][nx] = "."
        visited.add((nx, nz))
        stack.append((nx, nz))

    # Hand-tuned rooms and loop cuts keep the maze connected but less uniform.
    for x0, z0, x1, z1 in ((2, 4, 9, 9), (15, 2, 23, 8), (30, 5, 39, 12), (5, 22, 14, 29), (21, 18, 29, 25), (34, 29, 45, 37), (12, 38, 23, 46)):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                grid[z][x] = "."
    for z in range(1, height - 1):
        for x in range(1, width - 1):
            if grid[z][x] == "#" and rng.random() < 0.075:
                horizontal = grid[z][x - 1] == "." and grid[z][x + 1] == "."
                vertical = grid[z - 1][x] == "." and grid[z + 1][x] == "."
                if horizontal or vertical:
                    grid[z][x] = "."
    grid[start[1]][start[0]] = "."

    distance = bfs(grid, start)
    reachable = sum(value >= 0 for row in distance for value in row)
    walkable = sum(cell == "." for row in grid for cell in row)
    if reachable != walkable:
        raise ValueError(f"Level 0 is disconnected: {reachable}/{walkable} cells reachable")

    candidates: list[tuple[int, int, int]] = []
    for z in range(1, height - 1):
        for x in range(1, width - 1):
            if grid[z][x] != "#":
                continue
            approaches = [distance[z + dz][x + dx] for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)) if distance[z + dz][x + dx] >= 0]
            if approaches:
                candidates.append((min(approaches), x, z))
    preferred = [item for item in candidates if 60 <= item[0] <= 100]
    if not preferred:
        raise ValueError("No reachable Level 0 door wall 60-100 cells from spawn")
    door_distance, door_x, door_z = min(preferred, key=lambda item: (abs(item[0] - 82), -item[0]))
    grid[door_z][door_x] = "D"

    distance = bfs(grid, start)
    bot_candidates = [(distance[z][x], x, z) for z in range(height) for x in range(width) if 20 <= distance[z][x] <= 36]
    if not bot_candidates:
        raise ValueError("No valid far nextbot spawn")
    bot_distance, bot_x, bot_z = min(bot_candidates, key=lambda item: (abs(item[0] - 28), item[2], item[1]))
    return grid, (door_x, door_z), (bot_x, bot_z), door_distance, bot_distance


def make_poolrooms() -> list[list[str]]:
    width = height = 32
    grid = [["#"] * width for _ in range(height)]
    for z in range(1, height - 1):
        for x in range(1, width - 1):
            grid[z][x] = "."
    for x in range(4, 29):
        if x not in (8, 9, 20, 21):
            grid[10][x] = "#"
    for z in range(5, 28):
        if z not in (13, 14, 23, 24):
            grid[z][15] = "#"
    for x in range(2, 25):
        if x not in (6, 7, 17, 18):
            grid[22][x] = "#"
    for x, z in ((6, 5), (10, 5), (20, 6), (25, 6), (5, 16), (10, 17), (21, 16), (26, 18), (6, 27), (13, 27), (22, 27), (27, 26)):
        grid[z][x] = "#"
    for x0, z0, x1, z1 in ((18, 12, 28, 19), (3, 24, 11, 29), (3, 12, 11, 19)):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                if grid[z][x] == ".":
                    grid[z][x] = "W"
    for z in range(1, height - 1):
        for x in range(1, width - 1):
            if grid[z][x] != ".":
                continue
            if any(grid[z + dz][x + dx] == "W" for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                grid[z][x] = "E"
    grid[2][2] = "."
    distance = bfs(grid, (2, 2))
    walkable = sum(cell in (".", "W", "E") for row in grid for cell in row)
    reachable = sum(value >= 0 for row in distance for value in row)
    if reachable != walkable:
        raise ValueError(f"Poolrooms is disconnected: {reachable}/{walkable} cells reachable")
    return grid


def write_maps(path: Path, level0: list[list[str]], pool: list[list[str]], door: tuple[int, int], bot: tuple[int, int], door_distance: int, bot_distance: int) -> None:
    lines = [
        "/* Generated and BFS-verified by tools/convert_textures.py. */",
        "#ifndef BACKROOMS_GENERATED_LEVEL_MAPS_H",
        "#define BACKROOMS_GENERATED_LEVEL_MAPS_H",
        "#define LEVEL0_W 50",
        "#define LEVEL0_H 50",
        "#define POOL_W 32",
        "#define POOL_H 32",
        "#define LEVEL0_SPAWN_X 5.5f",
        "#define LEVEL0_SPAWN_Z 5.5f",
        "#define POOL_SPAWN_X 2.5f",
        "#define POOL_SPAWN_Z 2.5f",
        f"#define LEVEL0_DOOR_X {door[0]}",
        f"#define LEVEL0_DOOR_Z {door[1]}",
        f"#define LEVEL0_DOOR_DISTANCE {door_distance}",
        f"#define NEXTBOT_SPAWN_X {bot[0]}.5f",
        f"#define NEXTBOT_SPAWN_Z {bot[1]}.5f",
        f"#define NEXTBOT_INITIAL_DISTANCE {bot_distance}",
        "static const char LEVEL0_MAP[LEVEL0_H][LEVEL0_W + 1] = {",
    ]
    lines.extend(f'    "{"".join(row)}",' for row in level0)
    lines.append("};")
    lines.append("static const char POOL_MAP[POOL_H][POOL_W + 1] = {")
    lines.extend(f'    "{"".join(row)}",' for row in pool)
    lines.extend(["};", "#endif /* BACKROOMS_GENERATED_LEVEL_MAPS_H */", ""])
    atomic_write(path, "\n".join(lines).encode("ascii"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(os.environ.get("BACKROOMS_ASSET_DIR", "source_assets")))
    parser.add_argument("--generated", type=Path, default=Path("src/generated"))
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = Path.cwd()
    logger = Logger(project_root)
    if rgb565(255, 0, 0) != 0xF800 or rgb565(0, 255, 0) != 0x07E0 or rgb565(0, 0, 255) != 0x001F:
        raise RuntimeError("RGB565 self-test failed")
    logger.info("RGB565 self-test: red=F800 green=07E0 blue=001F PASS")

    missing = [name for name in TEXTURES if not (args.source / name).is_file()]
    if missing:
        raise FileNotFoundError("Missing required assets: " + ", ".join(missing))
    for source_name, (header_name, symbol, preserve_alpha) in TEXTURES.items():
        source = args.source / source_name
        width, height, pixels = read_png(source)
        converted = resize_cover(width, height, pixels, TEXTURE_SIZE, TEXTURE_SIZE)
        write_header(args.generated / header_name, symbol, converted, preserve_alpha)
        logger.info(f"Texture: {source_name} {width}x{height} -> {header_name} 128x128 RGB565" + ("+A8" if preserve_alpha else ""))

    for source_name, output_name, target_width, target_height in (
        ("PSP game menu icon.png", "ICON0.PNG", 144, 80),
        ("PSP game menu background.png", "PIC1.PNG", 480, 272),
    ):
        width, height, pixels = read_png(args.source / source_name)
        converted = resize_cover(width, height, pixels, target_width, target_height)
        write_png(args.assets / output_name, target_width, target_height, converted)
        logger.info(f"Menu: {source_name} {width}x{height} -> {output_name} {target_width}x{target_height}")

    level0, door, bot, door_distance, bot_distance = make_level0()
    pool = make_poolrooms()
    write_maps(args.generated / "level_maps.h", level0, pool, door, bot, door_distance, bot_distance)
    logger.info(f"Door location: x={door[0]}, z={door[1]}")
    logger.info(f"Shortest walking distance: {door_distance}")
    logger.info("Door reachable: YES")
    logger.info(f"Nextbot spawn: x={bot[0]}, z={bot[1]}")
    logger.info("Nextbot spawn reachable: YES")
    logger.info(f"Nextbot initial walking distance: {bot_distance}")
    logger.info(f"Preprocessing complete. Log: {logger.path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
