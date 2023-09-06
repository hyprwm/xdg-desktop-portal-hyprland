{
  stdenv,
  lib,
  cmake,
  qtbase,
  qtwayland,
  makeShellWrapper,
  wrapQtAppsHook,
  hyprland,
  slurp,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "hyprland-share-picker";
  inherit version;
  src = ../hyprland-share-picker;

  nativeBuildInputs = [
    cmake
    wrapQtAppsHook
    makeShellWrapper
  ];
  buildInputs = [
    qtbase
    qtwayland
  ];

  dontWrapQtApps = true;

  postInstall = ''
    wrapProgramShell $out/bin/hyprland-share-picker \
      "''${qtWrapperArgs[@]}" \
      --prefix PATH ":" ${lib.makeBinPath [slurp hyprland]}
  '';
}
