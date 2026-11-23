file_picker_data_sources = files([
  'Glob.cpp',
  'MimeDatabase.cpp',
  'PickerOptions.cpp',
])

if get_option('tests')
  file_picker_data_test = executable('hyprland-file-picker-data-test',
    [files('FilePickerDataTest.cpp'), file_picker_data_sources],
    dependencies: [dependency('hyprutils')],
  )
  test('hyprland-file-picker-data', file_picker_data_test)
endif
