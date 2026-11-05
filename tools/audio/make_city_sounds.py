"""
Synthesises the city sound effects for the demo project (22.05 kHz mono 16-bit WAV, like the
other demo sounds):

  door_open.wav     handle, slow creak, the door swinging shut - plays through the black fade
  door_locked.wav   a handle rattling against a lock
  door_unlock.wav   key in, turn, bolt slides back
  city_ambience.wav seamless loop: traffic rumble, passing cars, a distant horn
  room_tone.wav     seamless loop: mains hum, a fridge, air

    python3 tools/audio/make_city_sounds.py [output_dir]

Default output: projects/Demo/Audio. Seeded, so re-running gives identical files.
Requires NumPy.
"""
import os
import sys
import wave
import numpy as np

RATE = 22050
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "projects", "Demo", "Audio")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)


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


def lowpass(x, cutoff):
    """One-pole low-pass."""
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def resonator(x, freq, q):
    """Two-pole band-pass: rings at `freq` like a metal part or a wooden panel."""
    r = np.exp(-np.pi * freq / (q * RATE))
    c = 2 * r * np.cos(2 * np.pi * freq / RATE)
    y = np.zeros_like(x)
    for i in range(2, len(x)):
        y[i] = x[i] + c * y[i - 1] - r * r * y[i - 2]
    return y * (1 - r)


def place(buf, start, sound):
    i = int(start * RATE)
    n = min(len(sound), len(buf) - i)
    buf[i:i + n] += sound[:n]


def click(length=0.03, freq=2600, q=12, amp=1.0):
    n = rng.standard_normal(int(length * RATE)) * np.exp(-t(length) * 160)
    return (resonator(n, freq, q) * 3 + n * 0.3) * amp


def thud(freq=70, length=0.4, amp=1.0):
    tt = t(length)
    body = np.sin(2 * np.pi * freq * tt * (1 + 0.6 * np.exp(-tt * 30))) * np.exp(-tt * 11)
    knock = resonator(rng.standard_normal(len(tt)), 180, 4) * np.exp(-tt * 40) * 2
    return (body + knock) * amp


# ------------------------------------------------------------------ door_open
L = 1.9
door = np.zeros(int(L * RATE))
place(door, 0.02, click(0.04, 2200, 10, 0.8))           # handle down
place(door, 0.10, click(0.03, 3100, 14, 0.6))           # latch
# Creak: a stick-slip squeal - a pulse train whose rate wanders, filtered by the panel
ct = t(0.95)
rate = 180 + 120 * np.sin(2 * np.pi * 1.3 * ct) + 60 * np.sin(2 * np.pi * 4.1 * ct + 1) + rng.standard_normal(len(ct)).cumsum() * 0.4
phase = np.cumsum(rate / RATE)
pulses = (np.diff(np.floor(phase), prepend=0) > 0).astype(float) * (0.6 + 0.4 * rng.random(len(ct)))
creak = resonator(pulses, 900, 6) + resonator(pulses, 1750, 9) * 0.6 + resonator(pulses, 420, 5) * 0.8
env = np.minimum(1, ct / 0.12) * np.minimum(1, (0.95 - ct) / 0.25) * (0.7 + 0.3 * np.sin(2 * np.pi * 2.2 * ct))
place(door, 0.18, creak * env * 2.5)
place(door, 1.30, thud(62, 0.5, 1.6))                    # door swings shut behind you
place(door, 1.31, click(0.05, 1900, 8, 0.7))
save("door_open.wav", door)

# ------------------------------------------------------------------ door_locked
locked = np.zeros(int(0.75 * RATE))
for i, s in enumerate((0.0, 0.11, 0.30, 0.41)):
    place(locked, s, click(0.05, 2400 + 300 * (i % 2), 9, 1.0 if i % 2 == 0 else 0.7))
    place(locked, s + 0.005, thud(140, 0.12, 0.35))
save("door_locked.wav", locked)

# ------------------------------------------------------------------ door_unlock
unlock = np.zeros(int(0.8 * RATE))
scrape_t = t(0.22)
scrape = resonator(rng.standard_normal(len(scrape_t)), 4200, 5) * np.sin(np.pi * scrape_t / 0.22) * 0.8
place(unlock, 0.0, scrape)                               # key slides in
place(unlock, 0.30, click(0.03, 3300, 16, 0.7))          # turn
place(unlock, 0.42, click(0.06, 1500, 6, 1.0))           # bolt
place(unlock, 0.42, thud(110, 0.25, 0.6))
save("door_unlock.wav", unlock)


# ------------------------------------------------------------------ loops
def loop(x, fade=0.5):
    """Make a buffer loop seamlessly by cross-fading its tail into its head."""
    n = int(fade * RATE)
    head, body, tail = x[:n], x[n:-n], x[-n:]
    w = np.linspace(0, 1, n)
    return np.concatenate([tail * (1 - w) + head * w, body])


LEN = 10.5
city = np.zeros(int(LEN * RATE))
brown = np.cumsum(rng.standard_normal(len(city)))
brown -= lowpass(brown, 0.5)                              # remove drift
city += lowpass(brown, 220) * 0.02                        # distant traffic rumble
for start in (0.6, 3.4, 5.9, 8.2):                        # cars passing
    dur = rng.uniform(2.2, 3.2)
    tt = t(dur)
    swell = np.exp(-((tt - dur / 2) / (dur / 5)) ** 2)
    hiss = lowpass(rng.standard_normal(len(tt)), rng.uniform(500, 900)) * swell
    place(city, start, hiss * rng.uniform(0.5, 0.9))
ht = t(0.6)                                               # one far-off horn
horn = (np.sign(np.sin(2 * np.pi * 392 * ht)) + np.sign(np.sin(2 * np.pi * 494 * ht))) * np.minimum(1, ht / 0.03) * np.minimum(1, (0.6 - ht) / 0.1)
place(city, 7.1, lowpass(horn, 900) * 0.10)
save("city_ambience.wav", loop(city), 0.6)

LEN = 6.5
room = np.zeros(int(LEN * RATE))
rt = t(LEN)
room += np.sin(2 * np.pi * 60 * rt) * 0.25 + np.sin(2 * np.pi * 120 * rt) * 0.12 + np.sin(2 * np.pi * 180 * rt) * 0.05
room += lowpass(rng.standard_normal(len(room)), 400) * 0.6
room *= 1 + 0.05 * np.sin(2 * np.pi * 0.3 * rt)
save("room_tone.wav", loop(room), 0.35)
