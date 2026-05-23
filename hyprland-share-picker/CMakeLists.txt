cmake_minimum_required(VERSION 3.16)

project(
  hyprland-share-picker
  VERSION 0.1
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(
    DEPS
    REQUIRED
    hyprutils
    hyprtoolkit
    pixman-1
    aquamarine
    libdrm
)

set(PROJECT_SOURCES
    main.cpp
)

add_executable(hyprland-share-picker ${PROJECT_SOURCES})

target_include_directories(hyprland-share-picker PRIVATE
    ${DEPS_INCLUDE_DIRS}
)

target_link_directories(hyprland-share-picker PRIVATE
    ${DEPS_LIBRARY_DIRS}
)

target_link_libraries(hyprland-share-picker PRIVATE
    ${DEPS_LIBRARIES}
)

install(
    TARGETS hyprland-share-picker
    RUNTIME DESTINATION bin
)
