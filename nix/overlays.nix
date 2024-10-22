{
  self,
  inputs,
  lib,
}: let
  ver = lib.removeSuffix "\n" (builtins.readFile ../VERSION);

  mkJoinedOverlays = overlays: final: prev:
    lib.foldl' (attrs: overlay: attrs // (overlay final prev)) {} overlays;

  mkDate = longDate: (lib.concatStringsSep "-" [
    (builtins.substring 0 4 longDate)
    (builtins.substring 4 2 longDate)
    (builtins.substring 6 2 longDate)
  ]);

  version = ver + "+date=" + (mkDate (self.lastModifiedDate or "19700101")) + "_" + (self.shortRev or "dirty");
in {
  default = mkJoinedOverlays (with self.overlays; [
    xdg-desktop-portal-hyprland
    inputs.hyprlang.overlays.default
    inputs.hyprland-protocols.overlays.default
    inputs.hyprutils.overlays.default
    inputs.hyprwayland-scanner.overlays.default
    self.overlays.sdbuscpp
  ]);
  xdg-desktop-portal-hyprland = lib.composeManyExtensions [
    (final: prev: {
      xdg-desktop-portal-hyprland = final.callPackage ./default.nix {
        stdenv = prev.gcc13Stdenv;
        inherit (final.qt6) qtbase qttools wrapQtAppsHook qtwayland;
        inherit version;
      };
    })
  ];

  sdbuscpp = final: prev: {
    sdbus-cpp = prev.sdbus-cpp.overrideAttrs (self: super: {
      version = "2.0.0";

      src = final.fetchFromGitHub {
        owner = "Kistler-group";
        repo = "sdbus-cpp";
        rev = "refs/tags/v${self.version}";
        hash = "sha256-W8V5FRhV3jtERMFrZ4gf30OpIQLYoj2yYGpnYOmH2+g=";
      };
    });
  };
}
