picker_sources = files([
  'main.cpp',
  'Picker.cpp',
  'PickerData.cpp',
  'RegionPicker.cpp',
])

executable('hyprland-share-picker',
  picker_sources,
  dependencies: [
    dependency('hyprtoolkit'),
    dependency('hyprutils'),
    dependency('libdrm'),
  ],
  install: true,
)

if get_option('tests')
  picker_data_test = executable('hyprland-share-picker-data-test',
    files(['PickerDataTest.cpp', 'PickerData.cpp']),
  )
  test('hyprland-share-picker-data', picker_data_test)
endif
