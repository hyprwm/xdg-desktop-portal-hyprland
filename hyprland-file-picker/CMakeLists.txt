cmake_minimum_required(VERSION 3.19)

project(
  hyprland-file-picker
  VERSION 0.1
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(file_picker_deps REQUIRED IMPORTED_TARGET hyprutils>=0.14.4)
pkg_check_modules(file_picker_ui_deps REQUIRED IMPORTED_TARGET
                  hyprtoolkit>=0.7.0 libdrm)

set(FILE_PICKER_DATA_SOURCES Directory.cpp Glob.cpp MimeDatabase.cpp Places.cpp
                             PickerOptions.cpp QuickSearch.cpp)

add_executable(
  hyprland-file-picker main.cpp ui/FileList.cpp ui/FilePicker.cpp
                       ui/ListRow.cpp ${FILE_PICKER_DATA_SOURCES})
target_link_libraries(
  hyprland-file-picker PRIVATE PkgConfig::file_picker_deps
                               PkgConfig::file_picker_ui_deps)

install(TARGETS hyprland-file-picker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
  add_executable(hyprland-file-picker-data-test FilePickerDataTest.cpp
                                                ${FILE_PICKER_DATA_SOURCES})
  target_link_libraries(hyprland-file-picker-data-test
                        PRIVATE PkgConfig::file_picker_deps)
  add_test(NAME hyprland-file-picker-data
           COMMAND hyprland-file-picker-data-test)
endif()
