cmake_minimum_required(VERSION 3.10)

project(Nexus VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)

set(tmp_dir ${CMAKE_CURRENT_BINARY_DIR}/obj)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${tmp_dir})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${tmp_dir})

set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(CMAKE_BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_SYSTEM_NAME}) 
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/${CMAKE_SYSTEM_NAME}) 

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/CMakeModules)

# temp
file(COPY resources DESTINATION ${EXECUTABLE_OUTPUT_PATH}/Debug)

set(SOURCES
   ${PROJECT_SOURCE_DIR}/external/headerlibs.cpp
   ${PROJECT_SOURCE_DIR}/main.cpp


   # imgui
   external/src/imgui/imgui.cpp
   external/src/imgui/imgui_draw.cpp
   external/src/imgui/imgui_widgets.cpp
   external/src/imgui/imgui_tables.cpp

   external/src/imgui/backends/imgui_impl_opengl3.cpp
   external/src/imgui/backends/imgui_impl_sdl.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# SDL
add_subdirectory(external/src/sdl)
set(SDL_STATIC OFF)
include_directories(external/src/sdl/include)
target_link_libraries(${PROJECT_NAME} SDL2)

# GLAD
add_subdirectory(external/src/glad)
set(GLAD_ALL_EXTENSIONS ON)
include_directories(external/src/glad/include/glad)
target_link_libraries(${PROJECT_NAME} glad)

# stb headers
include_directories(external/src/stb)
include_directories(external/src/cuteheaders)

# imgui
include_directories(external/src/imgui)
include_directories(external/src/imgui/backends)

# miniaudio
include_directories(external/src/miniaudio)

# icon font
include_directories(external/src/iconfont)

# string match
include_directories(${PROJECT_SOURCE_DIR}/external)

# sqlite
add_subdirectory(external/src/sqlite)
include_directories(external/src/sqlite)
target_link_libraries(${PROJECT_NAME} SQLite3)