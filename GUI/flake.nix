{
  description = "Wisp — lightweight unofficial SoundCloud client (GTK4 + GStreamer GUI in C, Go scraper core)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      version = "1.0.0";
    in {
      packages = forAll (pkgs:
        let
          gst = with pkgs.gst_all_1; [ gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav ];
          wisp-core = pkgs.buildGoModule {
            pname = "wisp-core";
            inherit version;
            src = ./core;
            vendorHash = null; # standard library only
            ldflags = [ "-s" "-w" ];
            meta.mainProgram = "wisp-core";
          };
          wisp = pkgs.stdenv.mkDerivation {
            pname = "wisp";
            inherit version;
            src = ./ui;
            nativeBuildInputs = [ pkgs.pkg-config pkgs.wrapGAppsHook4 ];
            buildInputs = [ pkgs.gtk4 pkgs.glib pkgs.libsoup_3 pkgs.json-glib pkgs.glib-networking ] ++ gst;
            makeFlags = [ "PREFIX=${placeholder "out"}" ];
            preFixup = ''
              gappsWrapperArgs+=(--prefix PATH : ${pkgs.lib.makeBinPath [ wisp-core ]})
            '';
            meta = {
              description = "Lightweight unofficial SoundCloud client";
              mainProgram = "wisp";
              platforms = pkgs.lib.platforms.linux;
            };
          };
        in {
          inherit wisp wisp-core;
          default = wisp; # `nix build` builds the GUI and the Go core together
        });

      apps = forAll (pkgs: {
        default = { type = "app"; program = "${self.packages.${pkgs.system}.wisp}/bin/wisp"; };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = [ pkgs.pkg-config pkgs.go pkgs.gcc pkgs.gnumake ];
          buildInputs = with pkgs; [ gtk4 libsoup_3 json-glib ] ++ (with gst_all_1; [ gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav ]);
        };
      });
    };
}
