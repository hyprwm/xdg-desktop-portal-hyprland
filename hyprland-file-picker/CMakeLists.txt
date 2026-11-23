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

set(FILE_PICKER_DATA_SOURCES Glob.cpp MimeDatabase.cpp PickerOptions.cpp)

if(BUILD_TESTING)
  add_executable(hyprland-file-picker-data-test FilePickerDataTest.cpp
                                                ${FILE_PICKER_DATA_SOURCES})
  target_link_libraries(hyprland-file-picker-data-test
                        PRIVATE PkgConfig::file_picker_deps)
  add_test(NAME hyprland-file-picker-data
           COMMAND hyprland-file-picker-data-test)
endif()
