{
 description = "BlurCam — local face privacy for Linux";
 inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
 outputs = { self, nixpkgs }: let
  systems = [ "x86_64-linux" "aarch64-linux" ];
  each = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
  make = pkgs: let
   testModel = pkgs.fetchurl {
    url = "https://media.githubusercontent.com/media/opencv/opencv_zoo/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet/face_detection_yunet_2023mar.onnx";
    hash = "sha256-jyOD5N08+7RVPqhxgQf8BCMhDclk+fQoBgSATtJVL6Q=";
   };
  in pkgs.stdenv.mkDerivation {
   pname = "blurcam"; version = "0.1.0"; src = self;
   nativeBuildInputs = with pkgs; [ cmake pkg-config python3 ];
   buildInputs = with pkgs; [ opencv cli11 tomlplusplus ffmpeg SDL2 openssl curl ];
   nativeCheckInputs = with pkgs; [ ffmpeg ];
   doCheck = true;
   BLURCAM_TEST_MODEL = testModel;
   meta = { description = "Local face blur, pixelation and glitch with privacy fail-safe"; license = pkgs.lib.licenses.mit; platforms = systems; mainProgram = "blurcam"; };
  };
 in {
  packages = each (pkgs: { default = make pkgs; blurcam = make pkgs; });
  devShells = each (pkgs: { default = pkgs.mkShell { inputsFrom = [ (make pkgs) ]; packages = with pkgs; [ ninja clang-tools cppcheck git ffmpeg python3 ];
   LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [ pkgs.sdl3 ]; }; });
  checks = each (pkgs: { build = make pkgs; });
 };
}
