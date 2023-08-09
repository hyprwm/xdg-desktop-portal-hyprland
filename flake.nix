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
  in {
    overlays = import ./nix/overlays.nix {inherit self inputs lib;};

    packages = genSystems (system:
      (self.overlays.default pkgsFor.${system} pkgsFor.${system})
      // {default = self.packages.${system}.xdg-desktop-portal-hyprland;});

    formatter = genSystems (system: nixpkgs.legacyPackages.${system}.alejandra);
  };
}
