deps = [
  dependency('hyprutils', version: '>=0.11.0'),
  dependency('hyprtoolkit', version: '>=0.5.4'),
  dependency('pixman-1'),
  dependency('libdrm'),
]

sources = files([
  'main.cpp',
  'Picker.cpp',
  'Parsers.cpp',
])

executable('hyprland-share-picker',
  sources,
  dependencies: deps,
  install: true
)

# parser unit test: pure functions only, no toolkit/wayland deps.
if get_option('tests')
  picker_parsers_test = executable('picker-parsers-test',
    files('tests_parsers.cpp', 'Parsers.cpp'),
  )
  test('picker-parsers', picker_parsers_test)
endif
