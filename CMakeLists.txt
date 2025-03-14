cmake_minimum_required(VERSION 3.19)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/VERSION VER)
string(STRIP ${VER} VER)

project(
  xdg-desktop-portal-hyprland
  DESCRIPTION "An XDG-Destop-Portal backend for Hyprland (and wlroots)"
  VERSION ${VER})

set(CMAKE_MESSAGE_LOG_LEVEL "STATUS")
set(SYSTEMD_SERVICES
    ON
    CACHE BOOL "Install systemd service file")

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring XDPH in Debug with CMake")
  add_compile_definitions(HYPRLAND_DEBUG)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring XDPH in Release with CMake")
endif()

add_compile_definitions(XDPH_VERSION="${VER}")

include_directories(. "protocols/")

# configure
include(GNUInstallDirs)
set(LIBEXECDIR ${CMAKE_INSTALL_FULL_LIBEXECDIR})
configure_file(org.freedesktop.impl.portal.desktop.hyprland.service.in
               org.freedesktop.impl.portal.desktop.hyprland.service @ONLY)
if(SYSTEMD_SERVICES)
  configure_file(contrib/systemd/xdg-desktop-portal-hyprland.service.in
                 contrib/systemd/xdg-desktop-portal-hyprland.service @ONLY)
endif()

set(CMAKE_CXX_STANDARD 23)
add_compile_options(
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wno-narrowing
  -Wno-pointer-arith
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fpermissive>
  -Wno-address-of-temporary)

# dependencies
message(STATUS "Checking deps...")
add_subdirectory(hyprland-share-picker)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(
  deps
  REQUIRED
  IMPORTED_TARGET
  wayland-client
  wayland-protocols
  libpipewire-0.3>=1.1.82
  libspa-0.2
  libdrm
  libeis-1.0
  gbm
  hyprlang>=0.2.0
  hyprutils>=0.2.6
  hyprwayland-scanner>=0.4.2)

# check whether we can find sdbus-c++ through pkg-config
pkg_check_modules(SDBUS IMPORTED_TARGET sdbus-c++>=2.0.0)
if(NOT SDBUS_FOUND)
  include_directories("subprojects/sdbus-cpp/include/")
  add_subdirectory(subprojects/sdbus-cpp EXCLUDE_FROM_ALL)
  add_library(PkgConfig::SDBUS ALIAS sdbus-c++)
endif()

# same for hyprland-protocols
pkg_check_modules(HYPRLAND_PROTOS IMPORTED_TARGET hyprland-protocols)
if(HYPRLAND_PROTOS_FOUND)
  set(HYPRLAND_PROTOCOLS "${HYPRLAND_PROTOS_PREFIX}/share/hyprland-protocols")
else()
  set(HYPRLAND_PROTOCOLS "${CMAKE_SOURCE_DIR}/subprojects/hyprland-protocols")
endif()

pkg_search_module(UUID REQUIRED uuid)
file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(xdg-desktop-portal-hyprland ${SRCFILES})
target_link_libraries(
  xdg-desktop-portal-hyprland PRIVATE rt PkgConfig::SDBUS Threads::Threads
                                      PkgConfig::deps ${UUID_LIBRARIES})

# protocols
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
message(STATUS "Found wayland-protocols at ${WAYLAND_PROTOCOLS_DIR}")
pkg_get_variable(WAYLAND_SCANNER_DIR wayland-scanner pkgdatadir)
message(STATUS "Found wayland-scanner at ${WAYLAND_SCANNER_DIR}")

function(protocolnew protoPath protoName external)
  if(external)
    set(path ${protoPath})
  else()
    set(path ${WAYLAND_PROTOCOLS_DIR}/${protoPath})
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/${protoName}.cpp
           ${CMAKE_SOURCE_DIR}/protocols/${protoName}.hpp
    COMMAND hyprwayland-scanner --client ${path}/${protoName}.xml
            ${CMAKE_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_sources(xdg-desktop-portal-hyprland PRIVATE protocols/${protoName}.cpp
                                                     protocols/${protoName}.hpp)
endfunction()
function(protocolWayland)
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/protocols/wayland.cpp
           ${CMAKE_SOURCE_DIR}/protocols/wayland.hpp
    COMMAND hyprwayland-scanner --wayland-enums --client
            ${WAYLAND_SCANNER_DIR}/wayland.xml ${CMAKE_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_sources(xdg-desktop-portal-hyprland PRIVATE protocols/wayland.cpp
                                                     protocols/wayland.hpp)
endfunction()

protocolwayland()

protocolnew("${CMAKE_SOURCE_DIR}/protocols"
            "wlr-foreign-toplevel-management-unstable-v1" true)
protocolnew("${CMAKE_SOURCE_DIR}/protocols" "wlr-screencopy-unstable-v1" true)
protocolnew("${CMAKE_SOURCE_DIR}/protocols" "wlr-virtual-pointer-unstable-v1" true)
protocolnew("${CMAKE_SOURCE_DIR}/protocols" "virtual-keyboard-unstable-v1" true)
protocolnew("${HYPRLAND_PROTOCOLS}/protocols" "hyprland-global-shortcuts-v1"
            true)
protocolnew("${HYPRLAND_PROTOCOLS}/protocols" "hyprland-toplevel-export-v1"
            true)
protocolnew("${HYPRLAND_PROTOCOLS}/protocols" "hyprland-input-capture-v1"
            true)
protocolnew("stable/linux-dmabuf" "linux-dmabuf-v1" false)

# Installation
install(TARGETS hyprland-share-picker)
install(TARGETS xdg-desktop-portal-hyprland
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

install(FILES hyprland.portal
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/xdg-desktop-portal/portals")
install(
  FILES ${CMAKE_BINARY_DIR}/org.freedesktop.impl.portal.desktop.hyprland.service
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/services")
if(SYSTEMD_SERVICES)
  install(
    FILES
      ${CMAKE_BINARY_DIR}/contrib/systemd/xdg-desktop-portal-hyprland.service
    DESTINATION "lib/systemd/user")
endif()
