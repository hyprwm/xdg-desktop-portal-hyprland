{
  lib,
  stdenv,
  cmake,
  makeWrapper,
  pkg-config,
  wayland-scanner,
  wrapQtAppsHook,
  hyprland,
  hyprland-protocols,
  hyprlang,
  libdrm,
  mesa,
  pipewire,
  qtbase,
  qttools,
  qtwayland,
  sdbus-cpp,
  slurp,
  systemd,
  wayland,
  wayland-protocols,
  debug ? false,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-hyprland" + lib.optionalString debug "-debug";
  inherit version;

  src = ../.;

  nativeBuildInputs = [
    cmake
    makeWrapper
    pkg-config
    wayland-scanner
    wrapQtAppsHook
  ];

  buildInputs = [
    hyprland-protocols
    hyprlang
    libdrm
    mesa
    pipewire
    qtbase
    qttools
    qtwayland
    sdbus-cpp
    systemd
    wayland
    wayland-protocols
  ];

  cmakeBuildType =
    if debug
    then "Debug"
    else "RelWithDebInfo";

  dontStrip = true;

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
