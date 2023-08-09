{
  description = "xdg-desktop-portal-hyprland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # <https://github.com/nix-systems/nix-systems>
    systems.url = "github:nix-systems/default-linux";

    hyprland-protocols = {
      url = "github:hyprwm/hyprland-protocols";
      inputs.nixpkgs.follows = "nixpkgs";
    };
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
          self.overlays.default
          inputs.hyprland-protocols.overlays.default
        ];
      });
  in {
    overlays = import ./nix/overlays.nix {inherit self inputs lib;};

    packages = eachSystem (system:
      (self.overlays.default pkgsFor.${system} pkgsFor.${system})
      // {default = self.packages.${system}.xdg-desktop-portal-hyprland;});

    formatter = eachSystem (system: nixpkgs.legacyPackages.${system}.alejandra);
  };
}
