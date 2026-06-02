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
