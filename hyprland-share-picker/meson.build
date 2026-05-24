project('hyprland-share-picker', 'cpp',
  default_options: ['cpp_std=c++23'],
)

deps = [
  dependency('hyprutils'),
  dependency('hyprtoolkit'),
  dependency('aquamarine'),
  dependency('pixman-1'),
  dependency('libdrm'),
]

sources = files(
  'main.cpp',
)

executable(
  'hyprland-share-picker',
  sources,
  dependencies: deps,
  install: true,
)
