#!/usr/bin/env python3
"""Turns Hydrogen drum kits (recorded acoustic kits, GPL, from the Hydrogen drum machine project)
into NAM Home's simple layout:  <out>/<Kit name>/<drum>_<1-4>.wav  (soft -> hard), 24-bit WAV.

usage: make_drumkits.py <hydrogen drumkits folder> <out folder>
"""
import json, os, sys, xml.etree.ElementTree as ET
import numpy as np, soundfile as sf

DRUMS = ["kick", "snare", "rim", "hat", "hatopen", "hatpedal", "ride", "bell", "crash", "tom1", "tom2", "tom3"]

# For each output kit: which Hydrogen kit, and name keywords per drum (first match wins).
# Some Hydrogen kits hold two flavours (e.g. rock and jazz snares/rides), so one source can make two kits.
GENERIC = {
    "kick": [["kick"], ["bass drum"], ["bd"]],
    "snare": [["snare", "rock"], ["snare"], ["sd"]],
    "rim": [["stick"], ["rim"], ["side"]],
    "hat": [["closed"], ["hh", "cl"], ["hat"]],
    "hatopen": [["open"], ["hh", "op"]],
    "hatpedal": [["pedal"], ["foot"]],
    "ride": [["ride", "rock"], ["ride"]],
    "bell": [["bell"], ["cowbell"]],
    "crash": [["crash"], ["splash"], ["china"]],
    "tom1": [["tom", "hi"], ["high tom"], ["tom 1"], ["tom1"]],
    "tom2": [["tom", "mid"], ["tom 2"], ["tom2"]],
    "tom3": [["tom", "low"], ["floor"], ["tom 3"], ["tom3"]],
}
JAZZ = dict(GENERIC)
JAZZ.update({"snare": [["snare", "jazz"], ["snare"]], "ride": [["ride", "jazz"], ["ride"]], "crash": [["crash", "jazz"], ["crash"]]})

def tag(e):
    return e.tag.split('}')[-1]

def find(e, name):
    return [c for c in e.iter() if tag(c) == name]

def text(e, name, default=""):
    for c in e:
        if tag(c) == name:
            return (c.text or "").strip()
    return default

def read_kit(folder):
    xmlp = os.path.join(folder, "drumkit.xml")
    if not os.path.exists(xmlp):
        return None
    root = ET.parse(xmlp).getroot()
    insts = []
    for inst in find(root, "instrument"):
        name = text(inst, "name")
        layers = []
        for layer in find(inst, "layer"):
            fn = text(layer, "filename")
            if not fn:
                continue
            lo = float(text(layer, "min", "0") or 0)
            gain = float(text(layer, "gain", "1") or 1)
            layers.append((lo, os.path.join(folder, fn), gain))
        if not layers:
            fn = text(inst, "filename")
            if fn:
                layers.append((0.0, os.path.join(folder, fn), 1.0))
        if layers:
            insts.append((name, sorted(layers)))
    return insts

def pick(insts, rules, used):
    for words in rules:
        for name, layers in insts:
            n = name.lower()
            if all(w in n for w in words) and not (words == ["snare"] and "drum" in n and "bass" in n):
                if ("hat" in words or "closed" in words) and ("open" in n or "pedal" in n):
                    continue
                return name, layers
    return None, None

def write(path, data, rate):
    data = np.asarray(data, dtype=np.float32)
    if data.ndim == 1:
        data = np.stack([data, data], axis=1)
    data = data[: int(rate * 4.0)]
    # trim silence at the end, short fade
    env = np.max(np.abs(data), axis=1)
    loud = np.where(env > 1e-4)[0]
    if len(loud):
        data = data[: loud[-1] + 1]
    n = min(len(data), int(rate * 0.01))
    if n > 1:
        data[-n:] *= np.linspace(1, 0, n)[:, None]
    sf.write(path, data, rate, subtype="PCM_24")

def make(src_folder, out_folder, kit_name, rules):
    insts = read_kit(src_folder)
    if not insts:
        return None
    report = {}
    os.makedirs(out_folder, exist_ok=True)
    for drum in DRUMS:
        name, layers = pick(insts, rules[drum], set())
        if not layers:
            continue
        # up to 4 layers, spread from soft to hard
        idx = sorted(set(np.linspace(0, len(layers) - 1, min(4, len(layers))).round().astype(int).tolist()))
        k = 0
        peak = 0.0
        loaded = []
        for i in idx:
            _, path, gain = layers[i]
            if not os.path.exists(path):
                continue
            d, rate = sf.read(path, always_2d=True)
            d = d[:, :2] * gain
            loaded.append((d, rate))
            peak = max(peak, float(np.max(np.abs(d))) if d.size else 0.0)
        if not loaded:
            continue
        norm = 0.9 / peak if peak > 0 else 1.0   # loudest layer of each drum near full scale; softer layers keep their level
        for d, rate in loaded:
            k += 1
            write(os.path.join(out_folder, f"{drum}_{k}.wav"), d * norm, rate)
        report[drum] = {"from": name, "layers": k}
    if "kick" not in report or "snare" not in report:
        return None
    return report

def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    summary = {}
    for kit in sorted(os.listdir(src)):
        folder = os.path.join(src, kit)
        if not os.path.isdir(folder):
            continue
        pretty = kit.replace("Kit", " kit").replace("_", " ").strip()
        r = make(folder, os.path.join(out, pretty), pretty, GENERIC)
        if r:
            summary[pretty] = r
            # a jazz flavour when the kit has jazz snare/ride
            insts = read_kit(folder)
            if any("jazz" in n.lower() for n, _ in insts):
                rj = make(folder, os.path.join(out, pretty + " (jazz)"), pretty + " (jazz)", JAZZ)
                if rj:
                    summary[pretty + " (jazz)"] = rj
        else:
            import shutil
            shutil.rmtree(os.path.join(out, pretty), ignore_errors=True)
    json.dump(summary, open(os.path.join(out, "kits.json"), "w"), indent=1)
    print(json.dumps(summary, indent=1))

if __name__ == "__main__":
    main()
