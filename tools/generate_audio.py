"""Render Tile Flow's Web Audio sound effects into mono WAVs for Maxmod."""

import hashlib
import io
import math
import struct
import sys
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

SAMPLE_RATE = 16000
GAIN = 5.72
PEAK_LIMIT = .84

# Five wave keys. Color and lane offsets use the browser's minor scale.
WAVE_KEYS = 5
COLOR_PITCHES = 17
LANE_PITCHES = 14
# At least three of the 25 pool + 5 paddle tiles are consumed per link.
CHAIN_PITCHES = 10 * WAVE_KEYS


@dataclass(frozen=True)
class Tone:
    midi: int
    duration: float
    gain: float
    at: float = 0
    attack: float = .012
    triangle: bool = False
    target: Optional[int] = None


def add_tone(samples, tone):
    frequency = 440 * 2 ** ((tone.midi - 69) / 12)
    sweep = 0 if tone.target is None else (tone.target - tone.midi) / 12
    phase = 0
    start = round(tone.at * SAMPLE_RATE)
    rise = (tone.gain / .0001) ** (1 / (tone.attack * SAMPLE_RATE))
    fall = (.0001 / tone.gain) ** (1 / ((tone.duration - tone.attack) * SAMPLE_RATE))
    gain = .0001

    for index in range(round(tone.duration * SAMPLE_RATE)):
        time = index / SAMPLE_RATE
        pitch = frequency * 2 ** (sweep * min(time / (tone.duration * .75), 1)) if sweep else frequency
        nyquist_gain = max(0, min(1, (SAMPLE_RATE / 2 - pitch) / 1000))
        sample = math.sin(phase)
        if tone.triangle:
            if pitch < 1500:
                sample = (2 / math.pi) * math.asin(sample)
            else:
                # Drop triangle harmonics as they approach the GBA Nyquist frequency.
                third = max(0, min(1, (SAMPLE_RATE / 2 - pitch * 3) / 1000))
                fifth = max(0, min(1, (SAMPLE_RATE / 2 - pitch * 5) / 1000))
                sample = (8 / math.pi ** 2) * (
                    sample - third * math.sin(3 * phase) / 9 + fifth * math.sin(5 * phase) / 25
                )
        samples[start + index] += sample * gain * nyquist_gain
        phase += math.tau * pitch / SAMPLE_RATE
        gain *= rise if time < tone.attack else fall


def move(offset):
    return [Tone(72 + offset, .073, .035)]


def catch_tile(offset):
    return [Tone(72 + offset, .31, .095, triangle=True), Tone(84 + offset, .34, .037, at=.028)]


def tumble(offset):
    return [
        Tone(62 + offset, .084, .076, attack=.005, triangle=True, target=55 + offset),
        Tone(39, .061, .05, attack=.004, target=31),
    ]


def place(offset):
    return [Tone(60 + offset, .21, .11, triangle=True), Tone(40, .13, .12, attack=.009, target=28)]


def throw_tile(offset):
    return [Tone(67 + offset, .25, .073, attack=.028, target=84 + offset)]


def highlight(offset):
    return [Tone(67 + offset, .46, .073, attack=.16, target=86 + offset)]


def arpeggio(offset, notes):
    return [Tone(60 + offset + note, .65, .12, at=index * .055, triangle=index % 3 == 0)
            for index, note in enumerate(notes)]


def start_wave(offset):
    return arpeggio(offset, (0, 7, 14))


def match(offset):
    return [Tone(45, .34, .19, attack=.008, target=25)] + arpeggio(offset, (0, 7, 12, 15, 19, 24))


def wave_clear(offset):
    melody = ((7, 0), (12, .16), (16, .32), (19, .51), (21, .70), (19, .87), (24, 1.02))
    tones = [Tone(60 + offset + note, .55 if index == 6 else .27, .105,
                  at=at, attack=.024, triangle=True)
             for index, (note, at) in enumerate(melody)]
    tones.append(Tone(48 + offset, .60, .075, at=.51))
    tones.extend(Tone(60 + offset + note, .58, .048, at=1.02, attack=.035)
                 for note in (0, 4, 7, 14))
    return tones


def game_over(offset):
    return [Tone(60 + offset + note, .49, .12, at=.09 + index * .12)
            for index, note in enumerate((0, -4, -7, -12))]


FAMILIES = (
    (move, LANE_PITCHES),
    (catch_tile, COLOR_PITCHES),
    (tumble, COLOR_PITCHES),
    (place, COLOR_PITCHES),
    (throw_tile, WAVE_KEYS),
    (highlight, CHAIN_PITCHES),
    (start_wave, WAVE_KEYS),
    (match, CHAIN_PITCHES),
    (wave_clear, WAVE_KEYS),
    (game_over, WAVE_KEYS),
)
SINGLES = {
    "miss": [Tone(51, .29, .12, attack=.014, triangle=True, target=29)],
    "blocked": [Tone(44, .29, .12, attack=.014, triangle=True, target=29)],
}


def write_if_changed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def render_wav(path, tones):
    length = max(round(tone.at * SAMPLE_RATE) + round((tone.duration + .018) * SAMPLE_RATE)
                 for tone in tones)
    samples = [0.0] * length
    for tone in tones:
        add_tone(samples, tone)
    # Preserve each envelope while leaving room for the BGM and other voices.
    gain = min(GAIN, PEAK_LIMIT / max(map(abs, samples))) * 32767
    stream = io.BytesIO()
    with wave.open(stream, "wb") as wav:
        wav.setparams((1, 2, SAMPLE_RATE, 0, "NONE", "not compressed"))
        wav.writeframes(struct.pack(f"<{length}h", *(round(sample * gain) for sample in samples)))
    write_if_changed(path, stream.getvalue())


def render(build):
    output = build / "sfx"
    header = build / "tile_flow_sound_items.h"
    stamp = build / "tile_flow_sound_version"
    sounds = dict(SINGLES)
    lines = ["#pragma once", "", '#include "bn_sound_items.h"', "", "namespace tf::effects", "{"]
    for recipe, count in FAMILIES:
        family = recipe.__name__
        lines.append(f"inline constexpr bn::sound_item {family}[] = {{")
        for offset in range(count):
            name = f"{family}_{offset:02d}"
            sounds[name] = recipe(offset)
            lines.append(f"    bn::sound_items::{name},")
        lines.append("};")
    lines.extend(("}", ""))

    digest = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    if stamp.exists() and stamp.read_text() == digest and header.is_file() and all(
        (output / f"{name}.wav").is_file() for name in sounds
    ):
        return

    output.mkdir(parents=True, exist_ok=True)
    for name, tones in sounds.items():
        render_wav(output / f"{name}.wav", tones)
    for old in output.glob("*.wav"):
        if old.stem not in sounds:
            old.unlink()
    write_if_changed(header, "\n".join(lines).encode("ascii"))
    stamp.write_text(digest)


if __name__ == "__main__":
    render(Path(sys.argv[1]))
