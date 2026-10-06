#!/usr/bin/env python3
"""Offline, synthetic integration tests. No camera, model download or network access."""
import json, os, pathlib, subprocess, sys, tempfile, tomllib
BIN = str(pathlib.Path(sys.argv[1]).resolve())
checks = 0
def run(args, ok=True, **kw):
    global checks
    p = subprocess.run([str(x) for x in args], capture_output=True, timeout=60, **kw)
    checks += 1
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stderr.decode(errors="replace"))
    return p
def probe(p):
    return json.loads(run(["ffprobe", "-v", "error", "-count_frames", "-show_streams", "-of", "json", p]).stdout)
def call(*args, **kw): return run([BIN, *args], **kw)
with tempfile.TemporaryDirectory(prefix="blurcam-integration-") as d:
    d = pathlib.Path(d)
    os.environ["XDG_CONFIG_HOME"] = str(d / "config")
    os.environ["XDG_DATA_HOME"] = str(d / "data")
    call("--help")
    call("--version")
    for command in ["file", "preview", "webcam", "virtualcam", "effects", "models", "benchmark", "info", "devices", "completions"]:
        call(command, "--help")
    call("effects")
    call("effects", "glitch")
    call("effects", "no-such-effect", ok=False)
    call("info")
    call("devices")
    call("models", "list")
    for shell in ["bash", "zsh", "fish"]: call("completions", shell)
    call("file", "missing.mp4", "out.mp4", ok=False)
    call("info", "--confidence", "nan", ok=False)
    call("info", "--padding", "-1", ok=False)
    call("info", "--effect", "nonexistent", ok=False)
    call("info", "--mask", "face", ok=False)
    cfg = d / "saved.toml"
    call("info", "--preset", "privacy", "--pixel-size", "42", "--save-config", cfg)
    text = cfg.read_text()
    assert "42" in text and "privacy_failsafe = true" in text
    call("info", "--save-config", cfg, ok=False)
    cfg.write_text('[effects]\nstack=["censor"]\n[profiles.custom.effects]\nstack=["invert"]\n')
    saved = d / "profile.toml"
    call("info", "--config", cfg, "--profile", "custom", "--effect", "pixelate,rgbshift", "--save-config", saved)
    assert tomllib.loads(saved.read_text())["effects"]["stack"] == ["pixelate", "rgbshift"]
    # Optional model-dependent tests are enabled explicitly by the developer/CI.
    model = os.environ.get("BLURCAM_TEST_MODEL")
    if model:
        model = str(pathlib.Path(model).resolve())
        source = d / "input.mkv"
        run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x120:r=10:d=1", "-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-map", "0:v", "-map", "1:a", "-c:v", "ffv1", "-c:a", "pcm_s16le", source])
        target = d / "private.mkv"
        call("file", source, target, "--model", model, "--privacy-failsafe", "--codec", "ffv1", "--quiet")
        s = probe(target)["streams"]
        v = next(x for x in s if x["codec_type"] == "video")
        assert (v["width"], v["height"], v["nb_read_frames"], v["avg_frame_rate"]) == (160, 120, "10", "10/1")
        assert any(x["codec_type"] == "audio" and x["codec_name"] == "pcm_s16le" for x in s)
        for path in [source, target]:
            audio = run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a", "-c", "copy", "-f", "hash", "-"]).stdout
            if path == source: original_hash = audio
            else: assert original_hash == audio, "Audio changed"
        raw = run(["ffmpeg", "-v", "error", "-i", target, "-f", "rawvideo", "-pix_fmt", "bgr24", "-"]).stdout
        assert len(raw) == 10 * 160 * 120 * 3 and not any(raw), "Failsafe leaked source pixels"
        before = target.read_bytes()
        call("file", source, target, "--model", model, ok=False)
        assert before == target.read_bytes()
        call("file", source, source, "--overwrite", "--model", model, ok=False)
        call("file", source, "-o", d/"pipe.mkv", "--model", model, "--codec", "ffv1", "--privacy-failsafe", "--quiet")
        # Seekless stdin with a streaming-safe MKV source.
        call("file", "-", "-o", d/"stdin.mkv", "--model", model, "--codec", "ffv1", "--quiet", input=source.read_bytes())
        stream = call("file", source, "-o", "-", "--model", model, "--codec", "ffv1", "--privacy-failsafe", "--quiet").stdout
        (d/"stdout.mkv").write_bytes(stream)
        assert len(probe(d/"stdout.mkv")["streams"]) == 2
        # Variable timestamps are compared in seconds after container time-base rescaling.
        vfr = d/"vfr.mkv"
        run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x120:r=10:d=1", "-vf", "setpts='if(lt(N,5),N,5+(N-5)*2)/(10*TB)'", "-fps_mode", "vfr", "-c:v", "ffv1", vfr])
        call("file", vfr, d/"vfr-out.mkv", "--model", model, "--codec", "ffv1", "--quiet")
        def timestamps(path):
            r = run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames", "-show_entries", "frame=pts_time", "-of", "json", path])
            return [float(f["pts_time"]) for f in json.loads(r.stdout)["frames"]]
        assert timestamps(vfr) == timestamps(d/"vfr-out.mkv"), "VFR timestamps changed"
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        call("preview", "--input", source, "--model", model, "--limit-frames", "3", "--privacy-failsafe", env=env)
        live = call("virtualcam", "--input", source, "--output", "-", "--model", model, "--limit-frames", "3", "--privacy-failsafe", "--quiet").stdout
        assert len(live) == 4 * 160 * 120 * 3 and not any(live), "Raw live failsafe or final black frame failed"
        call("virtualcam", "--input", source, "--output", "/dev/does-not-exist", "--model", model, ok=False)
        call("benchmark", "--model", model, "--resolution", "160x120", "--frames", "5")
        bad = d/"bad.onnx"; bad.write_bytes(b"invalid")
        call("file", source, d/"bad.mkv", "--model", bad, ok=False)
        assert not (d/"bad.mkv").exists()
    else:
        print("Model-dependent integration skipped (set BLURCAM_TEST_MODEL); no network required.")
print(f"{checks} integration commands passed")
