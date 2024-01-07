cmake_minimum_required(VERSION 3.19)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/VERSION VER)
string(STRIP ${VER} VER)

project(xdg-desktop-portal-hyprland
    DESCRIPTION "An XDG-Destop-Portal backend for Hyprland (and wlroots)"
    VERSION ${VER}
)

set(CMAKE_MESSAGE_LOG_LEVEL "STATUS")

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
    message(STATUS "Configuring XDPH in Debug with CMake")
    add_compile_definitions(HYPRLAND_DEBUG)
else()
    add_compile_options(-O3)
    message(STATUS "Configuring XDPH in Release with CMake")
endif()

include_directories(
  .
  "protocols/"
)

# configure
include(GNUInstallDirs)
set(LIBEXECDIR ${CMAKE_INSTALL_FULL_LIBEXECDIR})
configure_file(org.freedesktop.impl.portal.desktop.hyprland.service.in org.freedesktop.impl.portal.desktop.hyprland.service @ONLY)
configure_file(contrib/systemd/xdg-desktop-portal-hyprland.service.in contrib/systemd/xdg-desktop-portal-hyprland.service @ONLY)

set(CMAKE_CXX_STANDARD 23)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-value
	-Wno-missing-field-initializers -Wno-narrowing -Wno-pointer-arith
	-fpermissive -Wno-address-of-temporary)

# dependencies
message(STATUS "Checking deps...")
add_subdirectory(hyprland-share-picker)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET wayland-client wayland-protocols libpipewire-0.3 libspa-0.2 libdrm gbm hyprlang>=0.2.0)

# check whether we can find sdbus-c++ through pkg-config
pkg_check_modules(SDBUS IMPORTED_TARGET sdbus-c++)
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
    set(HYPRLAND_PROTOCOLS "subprojects/hyprland-protocols")
endif()

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(xdg-desktop-portal-hyprland ${SRCFILES})
target_link_libraries(xdg-desktop-portal-hyprland PRIVATE rt PkgConfig::SDBUS Threads::Threads PkgConfig::deps)

# protocols
find_program(WaylandScanner NAMES wayland-scanner)
message(STATUS "Found WaylandScanner at ${WaylandScanner}")
execute_process(
    COMMAND pkg-config --variable=pkgdatadir wayland-protocols
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "Found wayland-protocols at ${WAYLAND_PROTOCOLS_DIR}")

function(protocol protoPath protoName external)
    if (external)
        execute_process(
            COMMAND ${WaylandScanner} client-header ${protoPath} protocols/${protoName}-protocol.h
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        execute_process(
            COMMAND ${WaylandScanner} private-code ${protoPath} protocols/${protoName}-protocol.c
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        target_sources(xdg-desktop-portal-hyprland PRIVATE protocols/${protoName}-protocol.c)
    else()
        execute_process(
            COMMAND ${WaylandScanner} client-header ${WAYLAND_PROTOCOLS_DIR}/${protoPath} protocols/${protoName}-protocol.h
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        execute_process(
            COMMAND ${WaylandScanner} private-code ${WAYLAND_PROTOCOLS_DIR}/${protoPath} protocols/${protoName}-protocol.c
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        target_sources(xdg-desktop-portal-hyprland PRIVATE protocols/${protoName}-protocol.c)
    endif()
endfunction()

protocol("protocols/wlr-foreign-toplevel-management-unstable-v1.xml" "wlr-foreign-toplevel-management-unstable-v1" true)
protocol("protocols/wlr-screencopy-unstable-v1.xml" "wlr-screencopy-unstable-v1" true)
protocol("${HYPRLAND_PROTOCOLS}/protocols/hyprland-global-shortcuts-v1.xml" "hyprland-global-shortcuts-v1" true)
protocol("${HYPRLAND_PROTOCOLS}/protocols/hyprland-toplevel-export-v1.xml" "hyprland-toplevel-export-v1" true)
protocol("unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml" "linux-dmabuf-unstable-v1" false)

# Installation
install(TARGETS hyprland-share-picker)
install(TARGETS xdg-desktop-portal-hyprland DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

install(FILES hyprland.portal DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/xdg-desktop-portal/portals")
install(FILES ${CMAKE_BINARY_DIR}/org.freedesktop.impl.portal.desktop.hyprland.service DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/services")
install(FILES ${CMAKE_BINARY_DIR}/contrib/systemd/xdg-desktop-portal-hyprland.service DESTINATION "lib/systemd/user")
