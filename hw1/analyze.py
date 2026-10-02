"""Read generated WAVs, estimate complex gain, and draw dependency-free SVG figures.

Usage: python analyze.py audio_examples figure
"""
from __future__ import annotations

import cmath
import math
from pathlib import Path
import sys
import wave

FS = 8000
FREQUENCIES = (100, 400, 3000)


def read_complex(path: Path) -> tuple[int, list[complex]]:
    with wave.open(str(path), "rb") as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        fs = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    import struct
    samples = struct.unpack("<" + "h" * (len(raw) // 2), raw)
    return fs, [complex(samples[i + 1], samples[i]) / 32768
                for i in range(0, len(samples), 2)]


def coefficient(data: list[complex], fs: int, f: int) -> complex:
    start = fs // 10  # discard startup transient
    block = data[start:]
    return sum(value * cmath.exp(-2j * math.pi * f * (start + i) / fs)
               for i, value in enumerate(block)) / len(block)


def path_for(values: list[complex], x0: float, x1: float, y0: float,
             half_height: float) -> str:
    scale = (x1 - x0) / (len(values) - 1)
    return " ".join(("M" if i == 0 else "L") +
                    f"{x0 + i * scale:.2f},{y0 - value.real * half_height:.2f}"
                    for i, value in enumerate(values))


def comparison_svg(audio: Path, figure: Path) -> list[tuple[int, float, float]]:
    parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="810" viewBox="0 0 1100 810">',
        '<rect width="1100" height="810" fill="#f8fafc"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#16243a}.title{font-size:28px;font-weight:bold}.label{font-size:18px}.small{font-size:15px}</style>',
        '<text x="60" y="48" class="title">RC low-pass: input and filtered cosine channel</text>',
        '<text x="60" y="79" class="small">fs = 8000 Hz, fc = 400 Hz; samples after 0.1 s (startup removed)</text>',
        '<line x1="730" y1="72" x2="775" y2="72" stroke="#2563eb" stroke-width="3"/>',
        '<text x="785" y="77" class="small">input</text>',
        '<line x1="880" y1="72" x2="925" y2="72" stroke="#e5533d" stroke-width="3"/>',
        '<text x="935" y="77" class="small">output</text>',
    ]
    measurements = []
    for row, f in enumerate(FREQUENCIES):
        _, original = read_complex(audio / f"sincos_fs8000_f{f}.wav")
        _, filtered = read_complex(audio / f"filtered_fs8000_f{f}.wav")
        gain = coefficient(filtered, FS, f) / coefficient(original, FS, f)
        measurements.append((f, abs(gain), math.degrees(cmath.phase(gain))))
        base = 225 + row * 225
        count = max(16, round(FS * 3 / f))  # three cycles
        start = FS // 10
        o, y = original[start:start + count], filtered[start:start + count]
        parts += [
            f'<rect x="55" y="{base-116}" width="990" height="190" rx="10" fill="white" stroke="#d5deeb"/>',
            f'<line x1="86" y1="{base}" x2="1005" y2="{base}" stroke="#9ca3af"/>',
            f'<path d="{path_for(o, 86, 1005, base, 85)}" fill="none" stroke="#2563eb" stroke-width="2"/>',
            f'<path d="{path_for(y, 86, 1005, base, 85)}" fill="none" stroke="#e5533d" stroke-width="2"/>',
            f'<text x="86" y="{base-88}" class="label">{f} Hz</text>',
            f'<text x="750" y="{base-88}" class="small">gain {abs(gain):.4f}; phase {math.degrees(cmath.phase(gain)):.2f}°</text>',
            f'<text x="86" y="{base+103}" class="small">0</text>',
            f'<text x="925" y="{base+103}" class="small">{1000*(count-1)/FS:.2f} ms</text>',
        ]
    parts.append('</svg>')
    (figure / "filter_comparison.svg").write_text("\n".join(parts), encoding="utf-8")
    return measurements


def phasor_svg(figure: Path) -> None:
    # Independent graph preview. The required GeoGebra share link is added separately.
    width, height = 1000, 420
    funcs = (
        ("X(t)", "#2563eb", lambda t: math.sqrt(3)*math.cos(t-math.pi/3)),
        ("Y(t)", "#e5533d", lambda t: 3*math.sin(t+2*math.pi/3)),
        ("Z(t)", "#16a34a", lambda t: 2*math.sqrt(3)*math.cos(t)),
    )
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="#f8fafc"/>',
             '<style>text{font-family:Arial,sans-serif;fill:#16243a}.title{font-size:24px;font-weight:bold}.label{font-size:16px}</style>',
             '<text x="60" y="43" class="title">Phasor exercise: X(t), Y(t), Z(t)</text>',
             '<line x1="60" y1="210" x2="950" y2="210" stroke="#94a3b8"/>']
    for i, tick in enumerate((0, math.pi/2, math.pi, 3*math.pi/2, 2*math.pi)):
        x = 60 + 890*tick/(2*math.pi)
        parts.append(f'<line x1="{x:.1f}" y1="70" x2="{x:.1f}" y2="350" stroke="#e2e8f0"/>')
        parts.append(f'<text x="{x:.1f}" y="375" class="label">{("0","π/2","π","3π/2","2π")[i]}</text>')
    for idx, (label, color, func) in enumerate(funcs):
        points = []
        for i in range(501):
            theta = 2*math.pi*i/500
            points.append(f'{60+890*i/500:.2f},{210-37*func(theta):.2f}')
        parts.append(f'<polyline points="{" ".join(points)}" fill="none" stroke="{color}" stroke-width="3"/>')
        parts.append(f'<line x1="{70+250*idx}" y1="405" x2="{105+250*idx}" y2="405" stroke="{color}" stroke-width="3"/>')
        parts.append(f'<text x="{115+250*idx}" y="410" class="label">{label}</text>')
    parts.append('</svg>')
    (figure / "A3_wave_preview.svg").write_text("\n".join(parts), encoding="utf-8")


def main() -> None:
    audio = Path(sys.argv[1]); figure = Path(sys.argv[2])
    figure.mkdir(parents=True, exist_ok=True)
    phasor_svg(figure)
    for f, gain, phase in comparison_svg(audio, figure):
        print(f"{f:4d} Hz: measured gain {gain:.6f}, phase {phase:.3f} deg")


if __name__ == "__main__":
    main()
