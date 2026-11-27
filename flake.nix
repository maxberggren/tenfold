{
  # tenfold for NixOS (and Nix anywhere on Linux), built against nixpkgs' Qt:
  #
  #   nix run github:maxberggren/tenfold
  #   nix profile install github:maxberggren/tenfold
  #
  # or in a NixOS configuration, with the flake as an input:
  #
  #   environment.systemPackages = [ tenfold.packages.${pkgs.system}.default ];
  #   services.udev.packages = [ tenfold.packages.${pkgs.system}.default ];  # optional: no password for the pad
  #
  # Reading the trackpad asks for your password through pkexec, which needs
  # security.polkit.enable (on with every desktop). The udev rule above lets
  # the person at the computer read touchpads without it.
  description = "tenfold: an instrument played with all ten fingers on the trackpad";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAll (pkgs: let
        # The stem separator's weights (bin/model fetches the same file).
        model = pkgs.fetchurl {
          url = "https://huggingface.co/adefossez/HTDemucs/resolve/cbc8a9b1a87023b7fd74e7b3412e6321c0eab003/955717e8.safetensors";
          hash = "sha256-2foUEzz8wDSmdYkjuzqMqfjf0LWCE0ZDu/g/csF1dt0=";
        };
        # The restorer's weights, likewise.
        restoreModel = pkgs.fetchurl {
          url = "https://huggingface.co/JusperLee/Apollo/resolve/c68bd80fdd9c0d93d2f4a833cb154624f660a561/pytorch_model.bin";
          hash = "sha256-mdmvfx/yDmPDkwNVE6ZVOSgY1mtNf8I9ZYF1wfFejXY=";
        };
      in {
        default = pkgs.stdenv.mkDerivation {
          pname = "tenfold";
          version = "0.5.0";
          src = self;

          nativeBuildInputs = [ pkgs.qt6.qmake pkgs.qt6.wrapQtAppsHook pkgs.pkg-config ];
          buildInputs = [ pkgs.qt6.qtbase pkgs.qt6.qtdeclarative pkgs.qt6.qtwayland pkgs.pipewire ];

          # For any processor of the architecture, like the other packages.
          qmakeFlags = [ "CONFIG+=portable" ];

          installPhase = ''
            runHook preInstall
            install -Dm755 tenfold $out/bin/tenfold
            install -Dm644 pkgbuild/tenfold.desktop $out/share/applications/tenfold.desktop
            install -Dm644 pkgbuild/tenfold.svg $out/share/icons/hicolor/scalable/apps/tenfold.svg
            install -Dm644 extras/70-tenfold.rules $out/lib/udev/rules.d/70-tenfold.rules
            install -Dm644 LICENSE $out/share/licenses/tenfold/LICENSE
            install -Dm644 ${model} $out/share/tenfold/htdemucs.safetensors
            install -Dm644 extras/LICENSE-htdemucs $out/share/licenses/tenfold/LICENSE-htdemucs
            install -Dm644 ${restoreModel} $out/share/tenfold/apollo.bin
            install -Dm644 extras/LICENSE-apollo $out/share/licenses/tenfold/LICENSE-apollo
            runHook postInstall
          '';

          meta = {
            description = "An instrument played with all ten fingers on the trackpad";
            homepage = "https://maxberggren.github.io/tenfold/";
            license = pkgs.lib.licenses.mit;
            platforms = systems;
            mainProgram = "tenfold";
          };
        };
      });

      apps = forAll (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.system}.default}/bin/tenfold";
        };
      });
    };
}
