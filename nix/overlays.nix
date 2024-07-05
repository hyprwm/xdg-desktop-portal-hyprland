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
  xdg-desktop-portal-hyprland = lib.composeManyExtensions [
    self.overlays.pipewire
    (final: prev: {
      xdg-desktop-portal-hyprland = final.callPackage ./default.nix {
        stdenv = prev.gcc13Stdenv;
        inherit (final) hyprland-protocols;
        inherit (final.qt6) qtbase qttools wrapQtAppsHook qtwayland;
        inherit version;
        inherit (inputs.hyprlang.packages.${prev.system}) hyprlang;
      };
    })
  ];

  # TODO: remove when https://nixpk.gs/pr-tracker.html?pr=322933 lands in unstable
  pipewire = final: prev: {
    pipewire = prev.pipewire.overrideAttrs (self: super: {
      version = "1.2.0";

      src = final.fetchFromGitLab {
        domain = "gitlab.freedesktop.org";
        owner = "pipewire";
        repo = "pipewire";
        rev = self.version;
        hash = "sha256-hjjiH7+JoyRTcdbPDvkUEpO72b5p8CbTD6Un/vZrHL8=";
      };

      mesonFlags = super.mesonFlags ++ [(final.lib.mesonEnable "snap" false)];
    });
  };
}
