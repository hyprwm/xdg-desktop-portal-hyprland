file_picker_data_sources = files([
  'Directory.cpp',
  'Glob.cpp',
  'MimeDatabase.cpp',
  'Places.cpp',
  'PickerOptions.cpp',
])

executable('hyprland-file-picker',
  [
    files([
      'main.cpp',
      'ui/FileList.cpp',
      'ui/FilePicker.cpp',
      'ui/ListRow.cpp',
    ]),
    file_picker_data_sources,
  ],
  dependencies: [
    dependency('hyprtoolkit'),
    dependency('hyprutils'),
    dependency('libdrm'),
  ],
  install: true,
)

if get_option('tests')
  file_picker_data_test = executable('hyprland-file-picker-data-test',
    [files('FilePickerDataTest.cpp'), file_picker_data_sources],
    dependencies: [dependency('hyprutils')],
  )
  test('hyprland-file-picker-data', file_picker_data_test)
endif
