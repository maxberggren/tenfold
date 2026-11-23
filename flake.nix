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
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "tenfold";
          version = "0.4.0";
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
