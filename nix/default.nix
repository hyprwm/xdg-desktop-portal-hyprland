{
  lib,
  stdenv,
  makeWrapper,
  meson,
  ninja,
  pkg-config,
  hyprlang,
  libdrm,
  mesa,
  pipewire,
  sdbus-cpp,
  systemd,
  wayland-protocols,
  wayland-scanner,
  qtbase,
  qttools,
  qtwayland,
  wrapQtAppsHook,
  hyprland,
  slurp,
  hyprland-protocols,
  wayland,
  debug ? false,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-hyprland";
  inherit version;

  src = ../.;

  mesonBuildType =
    if debug
    then "debug"
    else "release";

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    makeWrapper
    wrapQtAppsHook
  ];

  buildInputs = [
    hyprland-protocols
    libdrm
    mesa
    pipewire
    hyprlang
    qtbase
    qttools
    qtwayland
    sdbus-cpp
    systemd
    wayland
    wayland-protocols
  ];

  dontWrapQtApps = true;

  postInstall = ''
    wrapProgramShell $out/bin/hyprland-share-picker \
      "''${qtWrapperArgs[@]}" \
      --prefix PATH ":" ${lib.makeBinPath [slurp hyprland]}

    wrapProgramShell $out/libexec/xdg-desktop-portal-hyprland \
      --prefix PATH ":" ${lib.makeBinPath [(placeholder "out")]}
  '';

  meta = with lib; {
    homepage = "https://github.com/hyprwm/xdg-desktop-portal-hyprland";
    description = "xdg-desktop-portal backend for Hyprland";
    license = licenses.bsd3;
    maintainers = with maintainers; [fufexan];
    platforms = platforms.linux;
  };
}
