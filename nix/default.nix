{
  lib,
  stdenv,
  cmake,
  makeWrapper,
  pkg-config,
  wrapQtAppsHook,
  hyprland,
  hyprland-protocols,
  hyprlang,
  hyprutils,
  hyprwayland-scanner,
  libdrm,
  libgbm,
  pipewire,
  qtbase,
  qttools,
  qtwayland,
  sdbus-cpp_2,
  slurp,
  systemd,
  wayland,
  wayland-protocols,
  wayland-scanner,
  debug ? false,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-hyprland" + lib.optionalString debug "-debug";
  inherit version;

  src = ../.;

  depsBuildBuild = [
    pkg-config
  ];

  nativeBuildInputs = [
    cmake
    makeWrapper
    pkg-config
    wrapQtAppsHook
    hyprwayland-scanner
  ];

  buildInputs = [
    hyprland-protocols
    hyprlang
    hyprutils
    libdrm
    libgbm
    pipewire
    qtbase
    qttools
    qtwayland
    sdbus-cpp_2
    systemd
    wayland
    wayland-protocols
    wayland-scanner
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
