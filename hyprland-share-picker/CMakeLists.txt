cmake_minimum_required(VERSION 3.19)

project(
  hyprland-share-picker
  VERSION 0.1
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  picker_deps
  REQUIRED
  IMPORTED_TARGET
  hyprtoolkit
  hyprutils>=0.2.6
  libdrm)

add_executable(hyprland-share-picker main.cpp Picker.cpp PickerData.cpp
                                     RegionPicker.cpp)
target_link_libraries(hyprland-share-picker PRIVATE PkgConfig::picker_deps)

install(TARGETS hyprland-share-picker
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
  add_executable(hyprland-share-picker-data-test PickerDataTest.cpp
                                                  PickerData.cpp)
  add_test(NAME hyprland-share-picker-data
           COMMAND hyprland-share-picker-data-test)
endif()
