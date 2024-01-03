globber = run_command('find', '.', '-name', '*.cpp', check: true)
src = globber.stdout().strip().split('\n')

executable('xdg-desktop-portal-hyprland',
  [src, wl_proto_files],
  dependencies: [
    dependency('gbm'),
    dependency('hyprlang'),
    dependency('libdrm'),
    dependency('libpipewire-0.3'),
    dependency('sdbus-c++'),
    dependency('threads'),
    dependency('wayland-client'),
  ],
  include_directories: inc,
  install: true,
  install_dir: get_option('libexecdir')
)
