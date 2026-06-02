cmake_minimum_required(VERSION 3.16)

project(
  hyprland-share-picker
  VERSION 0.2
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  deps
  REQUIRED
  hyprutils>=0.11.0
  hyprtoolkit>=0.5.4
  pixman-1
  libdrm)

add_executable(hyprland-share-picker main.cpp Picker.cpp Parsers.cpp)

target_include_directories(hyprland-share-picker PRIVATE ${deps_INCLUDE_DIRS})
target_link_directories(hyprland-share-picker PRIVATE ${deps_LIBRARY_DIRS})
target_link_libraries(hyprland-share-picker PRIVATE ${deps_LIBRARIES})
target_compile_options(hyprland-share-picker PRIVATE ${deps_CFLAGS_OTHER})

install(TARGETS hyprland-share-picker)

# parser unit test: pure functions only, no toolkit/wayland deps.
option(PICKER_TESTS "Build the picker parser tests" OFF)
if(PICKER_TESTS)
  enable_testing()
  add_executable(picker-parsers-test tests_parsers.cpp Parsers.cpp)
  add_test(NAME picker-parsers COMMAND picker-parsers-test)
endif()
