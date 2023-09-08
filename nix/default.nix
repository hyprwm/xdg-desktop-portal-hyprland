{
  lib,
  stdenv,
  makeWrapper,
  meson,
  ninja,
  pkg-config,
  hyprland-share-picker,
  libdrm,
  mesa,
  pipewire,
  sdbus-cpp,
  systemd,
  wayland-protocols,
  wayland-scanner,
  grim,
  slurp,
  hyprland-protocols,
  wayland,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-hyprland";
  inherit version;

  src = ../.;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    makeWrapper
  ];

  buildInputs = [
    hyprland-protocols
    libdrm
    mesa
    pipewire
    sdbus-cpp
    systemd
    wayland
    wayland-protocols
  ];

  postInstall = ''
    wrapProgram $out/libexec/xdg-desktop-portal-hyprland --prefix PATH ":" ${lib.makeBinPath [hyprland-share-picker grim slurp]}
  '';

  meta = with lib; {
    homepage = "https://github.com/hyprwm/xdg-desktop-portal-hyprland";
    description = "xdg-desktop-portal backend for Hyprland";
    license = licenses.bsd3;
    maintainers = with maintainers; [fufexan];
    platforms = platforms.linux;
  };
}
