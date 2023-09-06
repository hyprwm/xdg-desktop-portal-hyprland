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
    hyprland-share-picker
    package-overrides
  ]);
  xdg-desktop-portal-hyprland = final: prev: {
    xdg-desktop-portal-hyprland = final.callPackage ./default.nix {
      stdenv = prev.gcc13Stdenv;
      inherit (final) hyprland-protocols hyprland-share-picker;
      inherit version;
    };
  };
  hyprland-share-picker = final: prev: {
    hyprland-share-picker = final.libsForQt5.callPackage ./hyprland-share-picker.nix {inherit version;};
  };
  package-overrides = final: prev: {
    sdbus-cpp = prev.sdbus-cpp.overrideAttrs (self: super: {
      version = "1.3.0";
      src = prev.fetchFromGitHub {
        repo = "sdbus-cpp";
        owner = "Kistler-Group";
        rev = "v${self.version}";
        hash = "sha256-S/8/I2wmWukpP+RGPxKbuO44wIExzeYZL49IO+KOqg4=";
      };
    });
  };
}
