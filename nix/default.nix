{
  lib,
  stdenv,
  makeWrapper,
  meson,
  ninja,
  pkg-config,
  cairo,
  hyprland-share-picker,
  libdrm,
  libjpeg,
  mesa,
  pango,
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
    cairo
    hyprland-protocols
    libdrm
    libjpeg
    mesa
    pango
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
