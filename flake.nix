{
  description = "xdg-desktop-portal-hyprland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    hyprland-protocols = {
      url = "github:hyprwm/hyprland-protocols";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = {
    self,
    nixpkgs,
    ...
  } @ inputs: let
    inherit (nixpkgs) lib;
    genSystems = lib.genAttrs [
      "aarch64-linux"
      "x86_64-linux"
    ];
    pkgsFor = genSystems (system:
      import nixpkgs {
        inherit system;
        overlays = [
          self.overlays.default
          inputs.hyprland-protocols.overlays.default
        ];
      });
    mkDate = longDate: (lib.concatStringsSep "-" [
      (builtins.substring 0 4 longDate)
      (builtins.substring 4 2 longDate)
      (builtins.substring 6 2 longDate)
    ]);
    version = "0.pre" + "+date=" + (mkDate (self.lastModifiedDate or "19700101")) + "_" + (self.shortRev or "dirty");
  in {
    overlays.default = final: prev: {
      xdg-desktop-portal-hyprland = final.callPackage ./nix/default.nix {
        inherit (final) hyprland-protocols hyprland-share-picker;
        inherit version;
      };

      hyprland-share-picker = final.libsForQt5.callPackage ./nix/hyprland-share-picker.nix {inherit version;};
    };

    packages = genSystems (system:
      (self.overlays.default pkgsFor.${system} pkgsFor.${system})
      // {default = self.packages.${system}.xdg-desktop-portal-hyprland;});

    formatter = genSystems (system: nixpkgs.legacyPackages.${system}.alejandra);
  };
}
