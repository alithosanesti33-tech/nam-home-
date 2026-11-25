#!/usr/bin/env python3
"""Slice the University of Iowa MIS saxophone scale recordings (free for any use) into one file per note.

Each source file is a chromatic scale played note by note with silence between notes. We find the
notes by their loudness, check each one's pitch, and write <out>/<sax>/<dyn>/<midi>.wav (mono, 24-bit).
"""
import sys, os, json, glob
import numpy as np
import soundfile as sf

def pitch_midi(x, sr):
    # autocorrelation pitch on the steady middle of the note
    n = len(x)
    seg = x[n // 3: n // 3 + min(8192, n // 3)]
    seg = seg - seg.mean()
    if len(seg) < 2048: return None
    lo, hi = int(sr / 1500), int(sr / 90)
    ac = np.correlate(seg, seg, 'full')[len(seg) - 1:]
    ac = ac / (ac[0] + 1e-12)
    lag = lo + int(np.argmax(ac[lo:hi]))
    # prefer the shortest lag with nearly the same peak (avoid octave-down errors)
    for div in (4, 3, 2):
        l2 = int(round(lag / div))
        if l2 >= lo and ac[max(lo, l2 - 2):l2 + 3].max() > 0.9 * ac[lag]:
            lag = max(range(max(lo, l2 - 2), l2 + 3), key=lambda k: ac[k]); break
    if 0 < lag < len(ac) - 1:
        a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
    else: d = 0
    f = sr / (lag + d)
    return 69 + 12 * np.log2(f / 440.0)

def slice_file(path, sax, dyn, out, report):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    hop = int(sr * 0.01)
    env = np.sqrt(np.convolve(x * x, np.ones(hop) / hop, 'same'))[::hop]
    peak = env.max()
    on = env > peak * 10 ** (-38 / 20)
    segs, start = [], None
    for i, v in enumerate(on):
        if v and start is None: start = i
        if not v and start is not None:
            if i - start > 25: segs.append((start, i))
            start = None
    if start is not None and len(on) - start > 25: segs.append((start, len(on)))
    # merge segments split by tiny gaps (< 80 ms)
    merged = []
    for s in segs:
        if merged and s[0] - merged[-1][1] < 8: merged[-1] = (merged[-1][0], s[1])
        else: merged.append(s)
    for (s, e) in merged:
        a = max(0, s * hop - int(0.03 * sr)); b = min(len(x), e * hop + int(0.15 * sr))
        note = x[a:b]
        m = pitch_midi(note, sr)
        if m is None: continue
        mi = int(round(m))
        cents = (m - mi) * 100
        d = os.path.join(out, sax, dyn); os.makedirs(d, exist_ok=True)
        # fade the very start/end so nothing clicks
        fi, fo = int(0.002 * sr), int(0.03 * sr)
        note = note.copy(); note[:fi] *= np.linspace(0, 1, fi); note[-fo:] *= np.linspace(1, 0, fo)
        dest = os.path.join(d, f"{mi}.wav")
        if os.path.exists(dest):   # keep the longer take if a note appears twice
            if len(sf.read(dest)[0]) >= len(note): continue
        sf.write(dest, note.astype(np.float32), sr, subtype='PCM_24')
        report.append({"sax": sax, "dyn": dyn, "midi": mi, "cents": round(float(cents), 1), "seconds": round(len(note) / sr, 2), "src": os.path.basename(path)})

def main(src, out):
    report = []
    for path in sorted(glob.glob(os.path.join(src, "*.aif*"))):
        name = os.path.basename(path)
        sax = "alto" if name.startswith("AltoSax") else "soprano"
        dyn = name.split(".")[2]
        slice_file(path, sax, dyn, out, report)
    with open(os.path.join(out, "report.json"), "w") as f: json.dump(report, f, indent=1)
    for sax in ("alto", "soprano"):
        for dyn in ("pp", "mf", "ff"):
            notes = sorted(r["midi"] for r in report if r["sax"] == sax and r["dyn"] == dyn)
            print(sax, dyn, len(notes), notes[:3], "...", notes[-3:])

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
