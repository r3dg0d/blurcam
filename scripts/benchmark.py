#!/usr/bin/env python3
"""Reproduce the synthetic benchmark matrix; save actual JSON, never invent results."""
import argparse, json, pathlib, subprocess
p = argparse.ArgumentParser()
p.add_argument("--binary", default="blurcam")
p.add_argument("--output", type=pathlib.Path, default=pathlib.Path("benchmarks"))
p.add_argument("--model")
p.add_argument("--frames", type=int, default=120)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
for effect in ("blur", "pixelate", "glitch"):
    for resolution, fps in (("1280x720",30),("1920x1080",30),("1920x1080",60),("2560x1440",60),("3840x2160",30),("3840x2160",60)):
        cmd = [a.binary,"benchmark","--effect",effect,"--resolution",resolution,"--fps",str(fps),"--frames",str(a.frames)]
        if a.model: cmd += ["--model", a.model]
        result = subprocess.run(cmd, capture_output=True, check=True, timeout=600)
        data = json.loads(result.stdout)
        (a.output / f"{effect}-{resolution}-{fps}.json").write_text(json.dumps(data, indent=2) + "\n")
        print(effect, resolution, fps, data["total_ms"], data["processing_fps"], flush=True)
