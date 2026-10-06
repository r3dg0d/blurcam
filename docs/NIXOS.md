# NixOS and Home Manager

```sh
nix run github:r3dg0d/blurcam -- --help
nix profile install github:r3dg0d/blurcam
blurcam models install
```

The flake builds a CPU-inference package for x86_64-linux and aarch64-linux; only x86_64
was tested locally. Its dependency revision is pinned in flake.lock. Driver modules are
system configuration, not part of the user package. Add this to your own configuration,
review it, then rebuild through your normal workflow:

```nix
{ config, pkgs, ... }: {
  boot.extraModulePackages = [ config.boot.kernelPackages.v4l2loopback ];
  boot.kernelModules = [ "v4l2loopback" ];
  boot.extraModprobeConfig = ''
    options v4l2loopback devices=1 video_nr=10 card_label="BlurCam" exclusive_caps=1
  '';
  users.users.YOUR_USERNAME.extraGroups = [ "video" ];
  environment.systemPackages = [ pkgs.v4l-utils ];
}
```

Do not duplicate an existing loopback configuration or select an occupied video number.
A new login may be required for group membership. Secure Boot can require module signing.
Keep kernel and out-of-tree module versions aligned; `config.boot.kernelPackages` ensures
that relationship. BlurCam reports missing/invalid backends; it never edits this config,
loads modules or elevates privileges automatically.

Verify and launch:

```sh
v4l2-ctl --list-devices
blurcam devices
blurcam virtualcam --device /dev/video0 --output /dev/video10 --preset max-privacy
```

Loopback `exclusive_caps=1` initially reports output capability and switches to capture
while BlurCam is running. Start BlurCam first, then open the browser/OBS camera selector.
For crash protection consider the v4l2loopback timeout control, which replaces stale video
with a timeout frame (verify its appearance before relying on it):

```sh
v4l2-ctl -d /dev/video10 --set-ctrl=timeout=1000
```

This optional command changes a runtime device control and is not run by BlurCam.

Home Manager with a flake input named `blurcam`:

```nix
{ pkgs, inputs, ... }: {
  home.packages = [ inputs.blurcam.packages.${pkgs.stdenv.hostPlatform.system}.default ];
  xdg.configFile."blurcam/config.toml".text = ''
    [tracking]
    privacy_failsafe = true
    padding = 0.4
    [effects]
    stack = ["censor"]
  '';
}
```

There is no automatically started camera service. Start capture explicitly when wanted.
Home Manager cannot install the kernel module; use NixOS for that part. The host Wayland
session and SDL choose the display backend; no X11 capture is required.

NVIDIA: use your normal NixOS NVIDIA driver configuration. This project does not alter it.
`nvidia-smi` must work. The default package uses CPU detection/effects. If FFmpeg exposes
NVENC and driver libraries are available, select `--codec h264_nvenc` explicitly:

```sh
LD_LIBRARY_PATH=/run/opengl-driver/lib blurcam file input.mp4 private.mp4 --codec h264_nvenc
```

CUDA inference requires separately building OpenCV with its CUDA DNN/cuDNN features;
there is no prebuilt GPU-inference flake output. No TensorRT engines or CUDA executable
weights are downloaded by model installation.
