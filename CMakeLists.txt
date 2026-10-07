cmake_minimum_required(VERSION 3.20)
project(ZeraLands VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ZL_BUILD_APP  "Build the ZeraLands editor (GLFW + OpenGL + Dear ImGui)" ON)
option(ZL_BUILD_CLI  "Build the headless zeralands_cli generator" ON)
option(ZL_WITH_WEBP  "Decode .webp textures (needed for the xtm PBR packs)" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- dependencies
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_Declare(lodepng
  GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
  GIT_TAG e584a8490db1b76b81bf298c11c09d3b55d9c270)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(stb lodepng json)

if(ZL_WITH_WEBP)
  set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
  set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v1.4.0)
  FetchContent_MakeAvailable(libwebp)
endif()

# ---------------------------------------------------------------- core library
add_library(zl_core STATIC
  src/core/Types.cpp
  src/core/Noise.cpp
  src/core/Heightfield.cpp
  src/core/Environments.cpp
  src/core/Eras.cpp
  src/core/Director.cpp
  src/core/Generator.cpp
  src/core/Erosion.cpp
  src/core/Hydrology.cpp
  src/core/Spline.cpp
  src/core/Networks.cpp
  src/core/Foliage.cpp
  src/core/Materials.cpp
  src/core/TextureLibrary.cpp
  src/core/ImageIO.cpp
  src/core/Exporter.cpp
  src/core/Project.cpp
  src/core/Sculpt.cpp
  src/core/Gis.cpp
  src/core/Brain.cpp
  ${lodepng_SOURCE_DIR}/lodepng.cpp)
target_include_directories(zl_core PUBLIC src ${stb_SOURCE_DIR} ${lodepng_SOURCE_DIR})
target_link_libraries(zl_core PUBLIC nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(zl_core PUBLIC Threads::Threads)
if(ZL_WITH_WEBP)
  target_compile_definitions(zl_core PRIVATE ZL_HAVE_WEBP=1)
  target_link_libraries(zl_core PRIVATE webpdecoder)
  target_include_directories(zl_core PRIVATE ${libwebp_SOURCE_DIR}/src)
endif()
if(MSVC)
  target_compile_options(zl_core PUBLIC /W3 /permissive- /utf-8)
  target_compile_definitions(zl_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
else()
  target_compile_options(zl_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- CLI
if(ZL_BUILD_CLI)
  add_executable(zeralands_cli src/cli/main_cli.cpp)
  target_link_libraries(zeralands_cli PRIVATE zl_core)
endif()

# ---------------------------------------------------------------- editor app
if(ZL_BUILD_APP)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9)
  FetchContent_MakeAvailable(glfw imgui)
  find_package(OpenGL REQUIRED)

  add_library(zl_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
  target_include_directories(zl_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
  target_link_libraries(zl_imgui PUBLIC glfw OpenGL::GL)

  set(ZL_APP_SOURCES
    src/app/main.cpp
    src/app/App.cpp
    src/app/AppPanels.cpp
    src/app/Renderer.cpp
    src/app/ProxyMeshes.cpp
    src/app/GLLoader.cpp)
  if(WIN32)
    list(APPEND ZL_APP_SOURCES assets/zeralands.rc)
    add_executable(ZeraLands WIN32 ${ZL_APP_SOURCES})
  else()
    add_executable(ZeraLands ${ZL_APP_SOURCES})
  endif()
  target_link_libraries(ZeraLands PRIVATE zl_core zl_imgui)
  if(WIN32)
    # Keep a normal main() even for the WIN32 subsystem build.
    if(MSVC)
      target_link_options(ZeraLands PRIVATE /ENTRY:mainCRTStartup)
    else()
      target_link_options(ZeraLands PRIVATE -mwindows)
    endif()
  endif()
  add_custom_command(TARGET ZeraLands POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:ZeraLands>/assets)
endif()
