# select either qt6 or qt5
qtdep = dependency('qt6', 'qt5', modules: ['Widgets'])
qtver = qtdep.version()
qt = import('qt' + qtver[0])

ui_files = qt.compile_ui(sources: 'mainpicker.ui')
moc = qt.compile_moc(headers: 'mainpicker.h')

sources = files([
  'main.cpp',
  'mainpicker.cpp',
  'mainpicker.h',
  'elidedbutton.h',
  'elidedbutton.cpp',
])

executable('hyprland-share-picker',
  sources,
  ui_files,
  moc,
  dependencies: [qtdep, dependency('hyprutils')],
  install: true
)
