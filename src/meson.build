globber = run_command('find', '.', '-name', '*.cpp', check: true)
src = globber.stdout().strip().split('\n')

executable('xdg-desktop-portal-hyprland',
  [src],
  dependencies: [
    client_protos,
    dependency('gbm'),
    dependency('hyprlang'),
    dependency('hyprutils'),
    dependency('libdrm'),
    dependency('libeis-1.0'),
    dependency('libpipewire-0.3'),
    dependency('sdbus-c++'),
    dependency('threads'),
    dependency('wayland-client'),
    dependency('uuid'),
  ],
  include_directories: inc,
  install: true,
  install_dir: get_option('libexecdir')
)
