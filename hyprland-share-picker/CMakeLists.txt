set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

cmake_minimum_required(VERSION 3.5)

project(hyprland-share-picker VERSION 0.1 LANGUAGES Go)

include(cmake/golang.cmake)
include(cmake/flags.cmake)

set(PROJECT_SOURCES
	main.go
	src/pkg/notebook/create.go
	src/pkg/region/create.go
	src/pkg/region/create_drawing_region.go
	src/pkg/screen/create.go
	src/pkg/screen/get_screen_list.go
	src/pkg/screen/type.go
	src/pkg/tools/create_button.go
	src/pkg/tools/scrollable_box.go
	src/pkg/window/create.go
	src/pkg/window/get_window_list.go
	src/pkg/window/type.go
	)

add_go_executable(hyprland-share-picker ${PROJECT_SOURCES})
