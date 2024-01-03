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
  ]);
  xdg-desktop-portal-hyprland = final: prev: {
    xdg-desktop-portal-hyprland = final.callPackage ./default.nix {
      stdenv = prev.gcc13Stdenv;
      inherit (final) hyprland-protocols;
      inherit (final.qt6) qtbase qttools wrapQtAppsHook qtwayland;
      inherit version;
      inherit (inputs.hyprlang.packages.${prev.system}) hyprlang;
    };
  };
}
