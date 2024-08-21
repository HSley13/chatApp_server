cmake_minimum_required(VERSION 3.30.0)
project(server_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(AWSSDK REQUIRED COMPONENTS s3)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia WebSockets)
qt_standard_project_setup()

set(PKG_CONFIG_EXECUTABLE "/opt/homebrew/bin/pkg-config")
find_package(PkgConfig REQUIRED)

add_subdirectory(database/)

qt_add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
                                                    server_main.cpp
                                                    server_manager.cpp
                    )

target_link_libraries(${PROJECT_NAME} PRIVATE database_library)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:${PROJECT_NAME}>
    ${CMAKE_SOURCE_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>
)

if (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    include_directories(${EMSCRIPTEN}/system/include)
endif()