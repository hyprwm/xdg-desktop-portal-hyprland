{
  lib,
  stdenv,
  cmake,
  makeWrapper,
  pkg-config,
  hyprland,
  hyprland-protocols,
  hyprlang,
  hyprtoolkit,
  hyprutils,
  hyprwayland-scanner,
  libdrm,
  libgbm,
  pipewire,
  sdbus-cpp_2,
  slurp,
  systemd,
  wayland,
  wayland-protocols,
  wayland-scanner,
  debug ? false,
  version ? "git",
  src,
}:
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-hyprland" + lib.optionalString debug "-debug";
  inherit version;

  inherit src;

  depsBuildBuild = [
    pkg-config
  ];

  nativeBuildInputs = [
    cmake
    makeWrapper
    pkg-config
    hyprwayland-scanner
  ];

  buildInputs = [
    hyprland-protocols
    hyprlang
    hyprtoolkit
    hyprutils
    libdrm
    libgbm
    pipewire
    sdbus-cpp_2
    systemd
    wayland
    wayland-protocols
    wayland-scanner
  ];

  cmakeBuildType = if debug then "Debug" else "RelWithDebInfo";

  dontStrip = true;

  postInstall = ''
    wrapProgramShell $out/bin/hyprland-share-picker \
      --prefix PATH ":" ${
        lib.makeBinPath [
          slurp
          hyprland
        ]
      }

    wrapProgramShell $out/libexec/xdg-desktop-portal-hyprland \
      --prefix PATH ":" ${lib.makeBinPath [ (placeholder "out") ]}
  '';

  meta = with lib; {
    mainProgram = "xdg-desktop-portal-hyprland";
    homepage = "https://github.com/hyprwm/xdg-desktop-portal-hyprland";
    description = "xdg-desktop-portal backend for Hyprland";
    license = licenses.bsd3;
    maintainers = with maintainers; [ fufexan ];
    platforms = platforms.linux;
  };
}
