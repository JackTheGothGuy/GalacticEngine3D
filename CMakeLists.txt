cmake_minimum_required(VERSION 3.20)
project(TwilightEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
#  Dependencies
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(glfw3  CONFIG REQUIRED)
find_package(GLEW   CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

# ---------------------------------------------------------------------------
#  Source file — support both src/ (VS default) and engine/ (Makefile)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer.cpp")
    set(MAIN_SRC src/renderer.cpp)
else()
    set(MAIN_SRC engine/renderer.cpp)
endif()

add_executable(TwilightEngine ${MAIN_SRC})

# ---------------------------------------------------------------------------
#  Include directories
# ---------------------------------------------------------------------------
target_include_directories(TwilightEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
#  Compiler definitions
# ---------------------------------------------------------------------------
target_compile_definitions(TwilightEngine PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<CONFIG:Debug>:_DEBUG>
)

# ---------------------------------------------------------------------------
#  Compiler options
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(TwilightEngine PRIVATE
        /W3
        /wd4244   # float<->double narrowing
        /wd4305   # double->float truncation
        /wd4996   # _CRT_SECURE_NO_WARNINGS (miniaudio uses fopen etc.)
        /MP
    )
else()
    target_compile_options(TwilightEngine PRIVATE -O2 -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
#  Link libraries
#  miniaudio needs platform audio backends on each OS
# ---------------------------------------------------------------------------
target_link_libraries(TwilightEngine PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
    assimp::assimp
)

if(WIN32)
    # miniaudio uses WinMM / WASAPI on Windows
    target_link_libraries(TwilightEngine PRIVATE winmm ole32)
elseif(APPLE)
    target_link_libraries(TwilightEngine PRIVATE
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
    )
else()
    # Linux: ALSA or PulseAudio (miniaudio auto-detects)
    target_link_libraries(TwilightEngine PRIVATE dl pthread m)
endif()

# ---------------------------------------------------------------------------
#  Single-header library downloads (only run at configure time if missing)
# ---------------------------------------------------------------------------

# stb_image
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/stb_image.h")
    message(STATUS "Downloading stb_image.h ...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/stb_image.h"
        STATUS _s SHOW_PROGRESS)
    list(GET _s 0 _c)
    if(NOT _c EQUAL 0)
        message(WARNING "stb_image.h download failed — place it in include/ manually")
    endif()
endif()

# miniaudio  (single-header, no vcpkg package needed)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/miniaudio.h")
    message(STATUS "Downloading miniaudio.h ...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/miniaudio.h"
        STATUS _s SHOW_PROGRESS)
    list(GET _s 0 _c)
    if(NOT _c EQUAL 0)
        message(WARNING "miniaudio.h download failed — place it in include/ manually")
    endif()
endif()

# ---------------------------------------------------------------------------
#  POST_BUILD: copy runtime assets next to the executable
#  This is what makes shader/asset path resolution work regardless of
#  what directory the IDE launches the binary from.
# ---------------------------------------------------------------------------

# shaders/
add_custom_command(TARGET TwilightEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:TwilightEngine>/shaders"
    COMMENT "Copying shaders/ -> output dir"
)

# assets/  (music + title image — only copy if the folder exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET TwilightEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:TwilightEngine>/assets"
        COMMENT "Copying assets/ -> output dir"
    )
else()
    message(STATUS
        "No assets/ directory found. Create it and add:\n"
        "  assets/title.png   — title screen background image\n"
        "  assets/music.ogg   — background music (OGG/MP3/WAV/FLAC)")
endif()