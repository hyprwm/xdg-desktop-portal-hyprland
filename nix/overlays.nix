{
  self,
  inputs,
  lib,
}: let
  mkJoinedOverlays = overlays: final: prev:
    lib.foldl' (attrs: overlay: attrs // (overlay final prev)) {} overlays;
  mkDate = longDate: (lib.concatStringsSep "-" [
    (builtins.substring 0 4 longDate)
    (builtins.substring 4 2 longDate)
    (builtins.substring 6 2 longDate)
  ]);
  version = "0.pre" + "+date=" + (mkDate (self.lastModifiedDate or "19700101")) + "_" + (self.shortRev or "dirty");
in {
  default = mkJoinedOverlays (with self.overlays; [
    xdg-desktop-portal-hyprland
    hyprland-share-picker
  ]);
  xdg-desktop-portal-hyprland = final: prev: {
    xdg-desktop-portal-hyprland = final.callPackage ./default.nix {
      inherit (final) hyprland-protocols hyprland-share-picker;
      inherit version;
    };
  };
  hyprland-share-picker = final: prev: {
    hyprland-share-picker = final.libsForQt5.callPackage ./hyprland-share-picker.nix {inherit version;};
  };
}
