globber = run_command('find', '.', '-name', '*.cpp', check: true)
src = globber.stdout().strip().split('\n')

hyprutils = dependency('hyprutils')
if not cpp_compiler.links('''
  #include <hyprutils/path/Path.hpp>
  #include <hyprutils/state/KVStateFile.hpp>
  int main() {
    Hyprutils::State::CKVStateFile file("unused");
    file.pruneOld(1024);
    return Hyprutils::Path::getDataDir("xdg-desktop-portal-hyprland").has_value();
  }
''', dependencies: hyprutils, args: '-std=c++23', name: 'hyprutils restore token APIs')
  error('hyprutils with CKVStateFile and getDataDir is required')
endif

executable('xdg-desktop-portal-hyprland',
  [src],
  dependencies: [
    client_protos,
    dependency('gbm'),
    dependency('hyprlang'),
    hyprutils,
    dependency('libdrm'),
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
