"""
Synthesises the combat sounds for the demo project (22.05 kHz mono 16-bit WAV):

  sword_swing.wav       a blade cutting air          sword_heavy.wav      a slower, deeper swing
  sword_hit.wav         a slash landing               sword_heavy_hit.wav  a finisher / slam landing
  dodge.wav             a quick roll                  body_fall.wav        a body hitting the street
  enemy_windup.wav      the puppet's rising shriek before it strikes
  enemy_die.wav         a puppet coming apart         enemy_spawn.wav      puppets crawling up
  seal.wav              the red seals slamming shut   area_clear.wav       the fight is over
  battle_music.wav      a seamless 140 BPM rock loop for fights

    python3 tools/audio/make_combat_sounds.py [output_dir]

Default output: projects/Demo/Audio. Seeded, so re-running gives identical files. Requires NumPy.
"""
import os
import sys
import wave
import numpy as np

RATE = 22050
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "projects", "Demo", "Audio")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(13)


def t(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def save(name, x, peak=0.85):
    x = x / max(1e-6, np.max(np.abs(x))) * peak
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((x * 32767).astype(np.int16).tobytes())
    print("wrote", name, f"{len(x) / RATE:.2f}s")


def onepole(x, cutoff):
    """One-pole low-pass with a cutoff that may change per sample."""
    cutoff = np.broadcast_to(np.asarray(cutoff, dtype=float), x.shape)
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = (1 - a[i]) * x[i] + a[i] * acc
        y[i] = acc
    return y


def bandpass(x, freq, q):
    """Two-pole resonator (freq may vary per sample)."""
    freq = np.broadcast_to(np.asarray(freq, dtype=float), x.shape)
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    for i in range(len(x)):
        r = np.exp(-np.pi * freq[i] / (q * RATE))
        c = 2 * r * np.cos(2 * np.pi * freq[i] / RATE)
        y0 = (1 - r) * x[i] + c * y1 - r * r * y2
        y[i] = y0
        y2, y1 = y1, y0
    return y


def env(n, attack, release):
    tt = np.arange(n) / RATE
    return np.minimum(1.0, tt / max(attack, 1e-4)) * np.exp(-np.maximum(0.0, tt - attack) / release)


def place(buf, start, sound):
    i = int(start * RATE)
    n = min(len(sound), len(buf) - i)
    buf[i:i + n] += sound[:n]


def whoosh(length, lo, hi, amount=1.0):
    """Noise through a band-pass that sweeps up and back down: air being cut."""
    n = int(length * RATE)
    s = np.sin(np.linspace(0, np.pi, n))
    f = lo + (hi - lo) * s ** 1.5
    return bandpass(rng.standard_normal(n), f, 2.5) * s ** 2 * amount


# ------------------------------------------------------------------ blades
save("sword_swing.wav", whoosh(0.26, 500, 3800) + 0.3 * whoosh(0.26, 1800, 7000))
save("sword_heavy.wav", whoosh(0.42, 250, 1800) + 0.25 * whoosh(0.42, 900, 4000))


def impact(length, ring, thud_freq, crunch):
    n = int(length * RATE)
    tt = t(length)
    noise = rng.standard_normal(n)
    transient = noise * env(n, 0.001, 0.015) * 2.0
    slice_ = bandpass(noise, 2600, 3) * env(n, 0.002, 0.06) * 1.5
    metal = sum(np.sin(2 * np.pi * f * tt + rng.random() * 6) * a for f, a in ((ring, 1.0), (ring * 2.76, 0.5), (ring * 5.4, 0.25)))
    metal *= env(n, 0.001, 0.12) * 0.25
    thud = np.sin(2 * np.pi * thud_freq * tt * (1 + 1.5 * np.exp(-tt * 40))) * env(n, 0.002, 0.08) * 1.2
    crunch_ = onepole(noise, 900) * env(n, 0.003, 0.1) * crunch
    return transient + slice_ + metal + thud + crunch_


save("sword_hit.wav", impact(0.35, 1450, 120, 1.5))
heavy = impact(0.6, 900, 70, 4.0)
place(heavy, 0.03, impact(0.3, 600, 50, 3.0) * 0.6)   # a second, lower thump right behind it
save("sword_heavy_hit.wav", heavy)

# ------------------------------------------------------------------ bodies and movement
save("dodge.wav", whoosh(0.3, 200, 900) * 0.8 + onepole(rng.standard_normal(int(0.3 * RATE)), 1500) * env(int(0.3 * RATE), 0.05, 0.08) * 0.5)
n = int(0.45 * RATE)
tt = t(0.45)
save("body_fall.wav", np.sin(2 * np.pi * 55 * tt * (1 + np.exp(-tt * 30))) * env(n, 0.002, 0.12) * 1.5 + onepole(rng.standard_normal(n), 700) * env(n, 0.002, 0.08))

# ------------------------------------------------------------------ puppets
n = int(0.5 * RATE)
tt = t(0.5)
f = 300 + 900 * (tt / 0.5) ** 2
phase = np.cumsum(f) / RATE
shriek = np.sign(np.sin(2 * np.pi * phase)) * 0.4 + np.sin(2 * np.pi * phase * 1.5) * 0.3
save("enemy_windup.wav", onepole(shriek, 3000) * np.minimum(1, tt / 0.3) * np.exp(-np.maximum(0, tt - 0.4) * 30) + whoosh(0.5, 300, 2000) * 0.3)
n = int(1.1 * RATE)
tt = t(1.1)
f = 220 * np.exp(-tt * 2.2) + 40
groan = np.sin(2 * np.pi * np.cumsum(f) / RATE) + 0.4 * np.sign(np.sin(2 * np.pi * np.cumsum(f * 1.01) / RATE))
crumble = np.zeros(n)
for k in range(28):
    place(crumble, rng.uniform(0.15, 1.0), bandpass(rng.standard_normal(int(0.04 * RATE)), rng.uniform(500, 2500), 4) * env(int(0.04 * RATE), 0.001, 0.01) * rng.uniform(0.5, 1.5))
save("enemy_die.wav", onepole(groan, 1200) * env(n, 0.01, 0.35) + crumble * 1.5)
n = int(1.2 * RATE)
tt = t(1.2)
rise = onepole(rng.standard_normal(n), 200 + 1800 * (tt / 1.2) ** 2) * (tt / 1.2) ** 2 * 3
save("enemy_spawn.wav", rise + np.sin(2 * np.pi * 45 * tt) * (tt / 1.2) ** 3 * 0.8)

# ------------------------------------------------------------------ seals and results
n = int(2.0 * RATE)
tt = t(2.0)
boom = np.sin(2 * np.pi * 38 * tt * (1 + 2 * np.exp(-tt * 12))) * env(n, 0.005, 0.6) * 1.5
shimmer = sum(np.sin(2 * np.pi * f * tt) for f in (523, 659, 784, 1046)) * env(n, 0.3, 0.6) * 0.12
save("seal.wav", boom + onepole(rng.standard_normal(n), 300) * env(n, 0.002, 0.3) + shimmer)
n = int(2.2 * RATE)
tt = t(2.2)
chord = np.zeros(n)
for i, freq in enumerate((329.6, 415.3, 493.9, 659.3)):   # E major, rolled
    start = i * 0.07
    tone = np.sin(2 * np.pi * freq * tt) + 0.3 * np.sin(4 * np.pi * freq * tt)
    chord += np.where(tt >= start, tone * env(n, 0.01, 0.9)[np.clip((tt - start) * RATE, 0, n - 1).astype(int)], 0)
save("area_clear.wav", chord, 0.7)

# ------------------------------------------------------------------ battle music
BPM = 140
beat = 60.0 / BPM
bars = 8
length = bars * 4 * beat
N = int(length * RATE)
music = np.zeros(N)


def kick():
    n = int(0.25 * RATE)
    tt = t(0.25)
    return np.sin(2 * np.pi * 50 * tt * (1 + 3 * np.exp(-tt * 35))) * env(n, 0.001, 0.09) * 1.4


def snare():
    n = int(0.22 * RATE)
    tt = t(0.22)
    return (bandpass(rng.standard_normal(n), 1800, 1.2) * 2.0 + np.sin(2 * np.pi * 190 * tt) * 0.6) * env(n, 0.001, 0.07)


def hat():
    n = int(0.05 * RATE)
    return bandpass(rng.standard_normal(n), 8000, 2) * env(n, 0.0005, 0.012) * 0.8


def saw(freq, length, detune=0.004):
    tt = t(length)
    s = 0
    for d in (-detune, 0, detune):
        s = s + (2 * ((tt * freq * (1 + d)) % 1.0) - 1)
    return s / 3


K, S, H = kick(), snare(), hat()
for b in range(bars * 4):
    at = b * beat
    if b % 2 == 0: place(music, at, K)
    if b % 4 == 3: place(music, at + beat * 0.5, K * 0.7)   # push into the next bar
    if b % 2 == 1: place(music, at, S)
    for e in range(2): place(music, at + e * beat / 2, H * (1.0 if e else 0.6))

# E minor riff: i - VI - VII - V (E, C, D, B), chugging eighths with accents
roots = [41.2, 41.2, 32.7, 36.7, 41.2, 41.2, 32.7, 30.9]   # bass frequencies per bar
for bar, root in enumerate(roots):
    for e in range(8):
        at = (bar * 4 + e * 0.5) * beat
        accent = 1.0 if e in (0, 3, 6) else 0.6
        note = saw(root * 2, beat * 0.45) * env(int(beat * 0.45 * RATE), 0.003, 0.12) * accent
        place(music, at, note * 0.9)
        # palm-muted power chord (root + fifth), distorted
        chord = saw(root * 4, beat * 0.42) + saw(root * 6, beat * 0.42) * 0.8
        chord = np.tanh(chord * 3.0) * env(len(chord), 0.002, 0.07 if accent < 1 else 0.16) * accent
        place(music, at, onepole(chord, 2400) * 0.45)
# A held lead line on top in the second half
lead_notes = [329.6, 311.1, 293.7, 246.9]
for i, freq in enumerate(lead_notes):
    at = (16 + i * 4) * beat
    ln = 4 * beat
    tt = t(ln)
    vib = 1 + 0.006 * np.sin(2 * np.pi * 5.5 * tt) * np.minimum(1, tt / 0.4)
    tone = np.tanh(np.sin(2 * np.pi * np.cumsum(freq * vib) / RATE) * 2.5)
    place(music, at, onepole(tone, 3000) * env(len(tone), 0.05, 1.8) * 0.25)
save("battle_music.wav", music, 0.8)
