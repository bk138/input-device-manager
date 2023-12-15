cmake_minimum_required(VERSION 3.0)
project(gnome-device-manager LANGUAGES C)

set(target_name gnome-device-manager)
set(version 0.3.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(CMAKE_GENERATOR MATCHES "Unix Makefiles|Ninja")
# some LSP servers expect compile_commands.json in the project root
add_custom_target(
    copy-compile-commands ALL
    ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif(CMAKE_GENERATOR MATCHES "Unix Makefiles|Ninja")

find_package(PkgConfig)

pkg_check_modules(gtk3 REQUIRED "gtk+-3.0 >= 3.22")
pkg_check_modules(xinput REQUIRED "xi >= 1.3")
pkg_check_modules(x11 REQUIRED x11)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${gtk3_INCLUDE_DIRS}
    ${xinput_INCLUDE_DIRS}
    ${x11_INCLUDE_DIRS}
)

link_directories(
    ${gtk3_LIBRARY_DIRS}
    ${xinput_LIBRARY_DIRS}
    ${x11_LIBRARY_DIRS}
)

set(sources
    src/main.c
)

add_executable(${target_name} ${sources})

target_link_libraries(${target_name}
    ${gtk3_LIBRARIES}
    ${xinput_LIBRARIES}
    ${x11_LIBRARIES}
)

