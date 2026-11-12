"""Bake Tile Flow's 8-bit backgrounds and 4-bit sprite sheets for Butano."""

import json
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parents[1] / "graphics"
GLYPHS = {
    "A": "0e11111f111111", "B": "1e11111e11111e", "C": "0e11101010110e",
    "D": "1e11111111111e", "E": "1f10101e10101f", "F": "1f10101e101010",
    "G": "0e11101711110f", "H": "1111111f111111", "I": "0e04040404040e",
    "J": "0702020212120c", "K": "11121418141211", "L": "1010101010101f",
    "M": "111b1515111111", "N": "11191915131311", "O": "0e11111111110e",
    "P": "1e11111e101010", "Q": "0e11111115120d", "R": "1e11111e141211",
    "S": "0f10100e01011e", "T": "1f040404040404", "U": "1111111111110e",
    "V": "11111111110a04", "W": "1111111515150a", "X": "11110a040a1111",
    "Y": "11110a04040404", "Z": "1f01020408101f",
    "0": "0e11131519110e", "1": "040c040404040e", "2": "0e11010204081f",
    "3": "1e01010601011e", "4": "02060a121f0202", "5": "1f101e0101110e",
    "6": "0608101e11110e", "7": "1f010204080808", "8": "0e11110e11110e",
    "9": "0e11110f01020c", "+": "0004041f040400", "-": "0000001f000000",
    "/": "01010204081010", "%": "19190204081313", ":": "00040400040400",
    ">": "10080402040810", "<": "01020408040201", ".": "00000000000c0c",
}

# Each component is on the GBA's RGB555 grid.
COLORS = [
    (248, 0, 248), (8, 8, 24), (8, 16, 32), (16, 24, 48), (24, 32, 64),
    (32, 48, 80), (40, 64, 104), (56, 88, 128), (72, 120, 152),
    (112, 152, 184), (184, 216, 232), (232, 248, 248),
    (24, 216, 232), (112, 240, 248), (48, 120, 152), (24, 72, 104),
    (240, 80, 184), (152, 120, 248), (248, 176, 72), (128, 232, 120),
    (248, 104, 88), (136, 64, 120), (64, 48, 112), (16, 32, 56),
    (48, 96, 120), (24, 24, 56), (40, 32, 80), (24, 40, 72),
]
TRANSPARENT, INK, DEEP, PANEL, PANEL_LIT, GRID, STEEL, EDGE, RIM, MUTED, LIGHT, WHITE, CYAN, ICE = range(14)

def palette(image, colors):
    entries = colors + [(0, 0, 0)] * (256 - len(colors))
    image.putpalette([channel for rgb in entries for channel in rgb])

def text(draw, x, y, label, fill, scale=1, spacing=1):
    for letter in label:
        for row, pattern in enumerate(bytes.fromhex(GLYPHS.get(letter, "00000000000000"))):
            for col in range(5):
                if pattern & (16 >> col):
                    left, top = x + col * scale, y + row * scale
                    draw.rectangle((left, top, left + scale - 1, top + scale - 1), fill)
        x += (5 + spacing) * scale

def backdrop():
    image = Image.new("P", (256, 256), INK)
    palette(image, COLORS)
    draw = ImageDraw.Draw(image)
    for y in range(160):
        fill = [INK, 2, 25, 2, INK][min(y // 32, 4)]
        draw.line((0, y, 239, y), fill)
    rng = random.Random(112)
    for _ in range(125):
        x, y = rng.randrange(240), rng.randrange(160)
        draw.point((x, y), rng.choice((3, 4, 5)))
    return image, draw

def frame(draw, x, y, w, h, fill=PANEL, accent=CYAN):
    points = [(x + 4, y), (x + w - 5, y), (x + w - 1, y + 4),
              (x + w - 1, y + h - 5), (x + w - 5, y + h - 1),
              (x + 4, y + h - 1), (x, y + h - 5), (x, y + 4)]
    draw.polygon(points, fill, STEEL)
    draw.line((x + 5, y, x + 25, y), accent)
    draw.line((x + w - 23, y + h - 1, x + w - 6, y + h - 1), EDGE)

def write_bg(name, image, bpp_mode="bpp_8", colors_count=32):
    image.save(OUT / (name + ".bmp"))
    (OUT / (name + ".json")).write_text(json.dumps({"type": "regular_bg", "bpp_mode": bpp_mode, "colors_count": colors_count}))

def board():
    image, draw = backdrop()
    # Match the game's projection from the rear of the belt to the paddle.
    def belt_x(edge, depth):
        return round(64 + (edge - 2.5) * (13.6 + 8.8 * depth))
    def belt_y(depth):
        return round(13 + 43 * depth + 28 * depth * depth)

    draw.polygon([(29, 11), (99, 11), (123, 86), (119, 94), (9, 94), (5, 86)], 23, RIM)
    for lane in range(5):
        draw.polygon([(belt_x(lane, 0), 13), (belt_x(lane + 1, 0), 13),
                      (belt_x(lane + 1, 1), 84), (belt_x(lane, 1), 84)],
                     DEEP if lane % 2 == 0 else PANEL)
    for index in range(21):
        depth = index / 20
        y = belt_y(depth)
        draw.line((belt_x(0, depth), y, belt_x(5, depth), y), GRID if index % 5 == 0 else PANEL_LIT)
    for index in range(6):
        draw.line((belt_x(index, 0), 13, belt_x(index, 1), 84), 24 if index in (0, 5) else STEEL)
    draw.line((30, 12, 98, 12), RIM)
    draw.line((9, 85, 119, 85), RIM)
    draw.line((10, 86, 118, 86), 15)
    draw.polygon([(7, 95), (121, 95), (121, 147), (117, 151), (11, 151), (7, 147)], GRID, RIM)
    draw.rectangle((9, 96, 119, 147), PANEL)
    for lane in range(5):
        left = 10 + lane * 22
        for row in range(5):
            y = 100 + row * 9
            draw.rectangle((left + 1, y, left + 19, y + 7), DEEP if row % 2 == 0 else 23)
            draw.line((left + 2, y + 7, left + 18, y + 7), PANEL_LIT)
        draw.line((left, 98, left, 145), STEEL)
        draw.line((left + 3, 148, left + 17, 148), 24)
    draw.line((120, 98, 120, 145), STEEL)
    draw.line((12, 150, 116, 150), 24)
    frame(draw, 127, 5, 109, 150)
    return image, draw

def play_screens():
    image, draw = board()
    text(draw, 135, 16, "WAVE", MUTED)
    draw.rectangle((135, 54, 227, 58), DEEP, STEEL)
    draw.line((135, 64, 227, 64), GRID)
    text(draw, 135, 73, "SCORE", MUTED)
    text(draw, 135, 92, "BEST", MUTED)
    draw.line((135, 103, 227, 103), GRID)
    text(draw, 135, 113, "DROPS", MUTED)
    draw.line((135, 128, 227, 128), GRID)
    write_bg("play_board", image)

    image, draw = board()
    draw.line((135, 34, 227, 34), GRID)
    draw.line((135, 124, 227, 124), GRID)
    write_bg("result_board", image)


def overlay_image(width=256, fill=TRANSPARENT):
    image = Image.new("P", (width, 256), fill)
    palette(image, COLORS)
    return image, ImageDraw.Draw(image)


def overlays():
    for name, label, accent in [("clear_popup", "WAVE CLEAR", CYAN),
                                ("over_popup", "GAME OVER", 18)]:
        image, draw = overlay_image()
        frame(draw, 30, 18, 180, 124, PANEL, accent)
        draw.rectangle((36, 24, 203, 49), DEEP)
        text(draw, (240 - len(label) * 14 + 4) // 2, 30, label, accent, 2, 2)
        draw.line((41, 51, 198, 51), GRID)
        draw.rectangle((41, 120, 198, 134), DEEP, GRID)
        # A separate 4bpp palette keeps the card's colors independent of the board fade.
        indices = [TRANSPARENT, DEEP, PANEL, GRID, STEEL, EDGE, accent]
        remap = {color: index for index, color in enumerate(indices)}
        image = image.point([remap.get(index, 0) for index in range(256)])
        palette(image, [COLORS[index] for index in indices])
        write_bg(name, image, "bpp_4_manual", 16)

    image, draw = overlay_image(512)
    frame(draw, 37, 20, 166, 38, PANEL, ICE)
    draw.line((55, 37, 184, 37), GRID)
    write_bg("wave_banner", image)

    # The two raster windows reveal this dither only between the ripple's edges.
    image, draw = overlay_image()
    for y in range(256):
        for x in range(y % 2, 256, 2):
            draw.point((x, y), RIM if y % 2 == 0 else 14)
    write_bg("wave_ring", image)


def title_screen():
    image, draw = backdrop()
    # The projected belt and the three tiles continue under the logo.
    for x in range(-54, 187, 21):
        draw.line((62 + (x - 62) // 5, 82, x, 159), GRID)
    for y in [85, 90, 97, 106, 117, 132, 151]:
        draw.line((0, y, 126, y), GRID)
    draw.line((5, 11, 34, 11), CYAN)
    draw.line((5, 12, 5, 28), 14)
    draw.line((85, 96, 118, 96), 24)
    text(draw, 10, 19, "TILE", 14, 4)
    text(draw, 8, 17, "TILE", LIGHT, 4)
    text(draw, 10, 55, "FLOW", 15, 4)
    text(draw, 8, 53, "FLOW", CYAN, 4)
    draw.line((8, 90, 110, 90), 24)
    text(draw, 9, 86, "ADVANCE", ICE, 1, 2)
    for x, y, shade, bright in [(31, 125, 21, 16), (61, 116, 15, 12), (90, 123, 22, 17)]:
        draw.polygon([(x - 11, y + 7), (x + 9, y + 6), (x + 20, y + 13),
                      (x + 6, y + 20), (x - 18, y + 14)], INK)
        draw.polygon([(x - 16, y), (x - 2, y - 10), (x + 16, y - 6),
                      (x + 16, y + 3), (x + 1, y + 13), (x - 16, y + 8)], shade, bright)
        draw.polygon([(x - 16, y), (x - 2, y - 10), (x + 16, y - 6), (x + 1, y + 4)], bright)
        draw.line((x - 13, y, x - 2, y - 8, x + 11, y - 5), WHITE)
        draw.line((x + 1, y + 5, x + 1, y + 11), bright)
    draw.rectangle((5, 138, 121, 155), INK)
    text(draw, 10, 138, "UP/DOWN ROW", LIGHT)
    text(draw, 10, 148, "LEFT/RIGHT SELECT", LIGHT)
    frame(draw, 125, 5, 111, 150)
    text(draw, 133, 97, "BEST", MUTED)
    for y in (42, 89, 118):
        draw.line((133, y, 228, y), GRID)
    draw.rectangle((133, 123, 228, 137), 15, 14)
    write_bg("title_screen", image)

def sprite_sheet(name, width, height, count):
    image = Image.new("P", (width, height * count))
    palette(image, [COLORS[0], COLORS[11], COLORS[12], COLORS[9], COLORS[5],
                    COLORS[10], COLORS[15], COLORS[18], COLORS[16]])
    config = {"type": "sprite", "height": height}
    def save():
        image.save(OUT / (name + ".bmp"), bits=8)
        (OUT / (name + ".json")).write_text(json.dumps(config))
    return ImageDraw.Draw(image), save

BELT_SIZES = 4
BELT_POSES = 12


def smoothstep(start, end, value):
    t = max(0, min(1, (value - start) / (end - start)))
    return t * t * (3 - 2 * t)


def belt_tile(draw, size, pose):
    # Rotate first around the leading bottom edge, then around the next edge
    # after the plate is upright. Project both visible faces onto the belt.
    phase = pose / BELT_POSES
    length = 7.3 + size * 1.25
    thickness = 1.9 + size * .16
    height_scale = .92
    depth_scale = (2.5 + size * .7) / (length + thickness)
    linear_center = -length / 2 + (length + thickness) * phase
    half_width = 5 + size
    y_origin = (size * BELT_POSES + pose) * 16
    angle = math.pi / 2 * (smoothstep(.12, .57, phase) + smoothstep(.65, .92, phase))
    sine, cosine = math.sin(angle), math.cos(angle)

    def rotate(z, height):
        if angle < math.pi / 2:
            return z * cosine + height * sine, -z * sine + height * cosine
        return thickness + z * cosine + (height - thickness) * sine, -z * sine + (height - thickness) * cosine

    def project(point):
        z, height = point
        return y_origin + round(11 + (z - linear_center) * depth_scale - height * height_scale)

    corners = [rotate(-length, 0), rotate(0, 0), rotate(0, thickness), rotate(-length, thickness)]
    left, right = 16 - half_width, 16 + half_width
    for index, start in enumerate(corners):
        end = corners[(index + 1) % len(corners)]
        dz, dh = end[0] - start[0], end[1] - start[1]
        if dh * height_scale - dz * depth_scale <= 0:
            continue
        top, bottom = sorted((project(start), project(end)))
        broad_face = index % 2 == 0
        if broad_face:
            draw.rectangle((left, top, right, bottom), 3)
            draw.line((left, top, right, top), 1)
            draw.line((left, bottom, right, bottom), 2)
            if bottom - top >= 4:
                middle = (top + bottom) // 2
                draw.line((left + 2, middle, right - 2, middle), 5, 2)
                draw.point((left + 1, top + 1), 2)
        else:
            draw.rectangle((left, top, right, bottom), 4)
            draw.line((left + 1, bottom, right - 1, bottom), 2)


def sprites():
    draw, save = sprite_sheet("glyphs", 8, 8, 94)
    for code in range(33, 127):
        text(draw, 0, (code - 33) * 8, chr(code), 1)
    save()

    # Shared shading indices get a different OBJ palette for each of the six tile colors.
    pool_start = BELT_SIZES * BELT_POSES
    draw, save = sprite_sheet("tiles", 32, 16, pool_start + 7)
    for size in range(BELT_SIZES):
        for pose in range(BELT_POSES):
            belt_tile(draw, size, pose)
    marks = [
        ["01110", "10001", "10001", "10001", "01110"],
        ["00100", "01010", "10001", "01010", "00100"],
        ["00100", "00100", "11111", "00100", "00100"],
        ["00100", "01010", "01010", "10001", "11111"],
        ["00000", "11111", "00000", "11111", "00000"],
        ["10001", "01010", "00100", "01010", "10001"],
    ]
    for index, mark in enumerate(marks):
        y = (pool_start + index) * 16 + 8
        draw.polygon([(7, y - 3), (24, y - 3), (25, y - 2), (25, y + 2),
                      (24, y + 3), (7, y + 3), (6, y + 2), (6, y - 2)], 3)
        draw.line((8, y - 3, 23, y - 3), 1)
        draw.line((7, y + 2, 24, y + 2), 2)
        draw.line((8, y + 3, 23, y + 3), 4)
        for row, line in enumerate(mark):
            for col, pixel in enumerate(line):
                if pixel == "1":
                    draw.point((14 + col, y - 2 + row), 1 if row < 4 else 5)
    y = (pool_start + 6) * 16 + 8
    draw.polygon([(7, y - 2), (24, y - 2), (25, y - 1), (25, y + 1),
                  (24, y + 2), (7, y + 2), (6, y + 1), (6, y - 1)], 3)
    draw.line((8, y - 2, 23, y - 2), 1)
    draw.line((7, y + 1, 24, y + 1), 2)
    draw.line((8, y + 2, 23, y + 2), 4)
    save()

    draw, save = sprite_sheet("paddle", 32, 8, 3)
    for index, accent in enumerate((2, 7, 8)):
        y = index * 8
        draw.polygon([(5, y + 2), (26, y + 2), (28, y + 4), (26, y + 6), (5, y + 6), (3, y + 4)], 6, accent)
        draw.line((7, y + 2, 24, y + 2), 1)
        draw.line((13, y + 6, 18, y + 6), 1)
    save()

    draw, save = sprite_sheet("drop_meter", 32, 8, 18)
    for limit in range(3, 6):
        for remaining in range(6):
            y = ((limit - 3) * 6 + remaining) * 8 + 3
            for index in range(limit):
                x = index * 6 + 3
                draw.polygon([(x, y - 2), (x + 2, y), (x, y + 2), (x - 2, y)], 2 if index < remaining else 4)
                if index < remaining:
                    draw.point((x, y - 1), 1)
    save()

    draw, save = sprite_sheet("goal_fill", 16, 8, 16)
    for index in range(16):
        y = index * 8 + 3
        draw.line((0, y, index, y), 1)
        draw.line((0, y + 1, index, y + 1), 2)
        draw.line((0, y + 2, index, y + 2), 2)
    save()

    draw, save = sprite_sheet("spark", 8, 8, 4)
    draw.polygon([(4, 1), (6, 4), (4, 6), (2, 4)], 2)
    draw.point((4, 3), 1)
    draw.polygon([(3, 10), (6, 11), (5, 14), (2, 13)], 2)
    draw.line((3, 10, 5, 11), 1)
    draw.line((2, 20, 5, 18), 1)
    draw.point((4, 28), 2)
    save()

    draw, save = sprite_sheet("ripple", 32, 32, 6)
    for index in range(6):
        y = index * 32 + 16
        width = 4 + index * 2
        height = 2 + index
        draw.ellipse((16 - width, y - height, 16 + width, y + height), outline=2 if index < 4 else 3)
        if index < 3:
            draw.arc((15 - width, y - height - 1, 17 + width, y + height + 1), 204, 328, 1)
    save()

if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    play_screens()
    title_screen()
    overlays()
    sprites()
