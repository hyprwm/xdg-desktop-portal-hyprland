{
  description = "xdg-desktop-portal-hyprland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # <https://github.com/nix-systems/nix-systems>
    systems.url = "github:nix-systems/default-linux";

    hyprland-protocols = {
      url = "github:hyprwm/hyprland-protocols";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.systems.follows = "systems";
    };

    hyprlang.url = "github:hyprwm/hyprlang";
  };

  outputs = {
    self,
    nixpkgs,
    systems,
    ...
  } @ inputs: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);
    pkgsFor = eachSystem (system:
      import nixpkgs {
        localSystem = system;
        overlays = [
          inputs.hyprland-protocols.overlays.default
          self.overlays.xdg-desktop-portal-hyprland
        ];
      });
  in {
    overlays = import ./nix/overlays.nix {inherit self inputs lib;};

    packages = eachSystem (system: {
      inherit (pkgsFor.${system}) xdg-desktop-portal-hyprland;
      default = self.packages.${system}.xdg-desktop-portal-hyprland;
    });

    formatter = eachSystem (system: nixpkgs.legacyPackages.${system}.alejandra);
  };
}
