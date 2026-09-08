"""Generate four-tap windowed-sinc filters without reading DSP ROM data.

The three banks approximate AX's 32/12/16 kHz filters. They do not reproduce
the hardware coefficient ROM. Every phase has unity DC gain in Q15.
"""
from pathlib import Path
import math
import sys


def sinc(x):
    return 1.0 if abs(x) < 1e-12 else math.sin(math.pi * x) / (math.pi * x)


def coefficients():
    values = []
    for cutoff in (1.0, 12 / 32, 16 / 32):
        for phase in range(128):
            center = 1 + phase / 128
            raw = [cutoff * sinc(cutoff * (tap - center)) * sinc((tap - center) / 2)
                   for tap in range(4)]
            scale = 32768 / sum(raw)
            weights = [round(value * scale) for value in raw]
            weights[1] += 32768 - sum(weights)
            values.extend(weights)
    return values


if __name__ == '__main__':
    dest = Path(sys.argv[1])
    values = coefficients()
    text = '// Generated from windowed-sinc equations, not console firmware.\n'
    text += 'static const s32 resample_coefficients[1536] = {\n'
    text += '\n'.join(','.join(map(str, values[i:i+16])) + ',' for i in range(0, len(values), 16))
    text += '\n};\n'
    if not dest.exists() or dest.read_text() != text:
        dest.write_text(text)
