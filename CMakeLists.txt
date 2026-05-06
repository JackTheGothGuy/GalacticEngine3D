cmake_minimum_required(VERSION 3.20)
project(TwilightEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
#  Dependencies (resolved by vcpkg toolchain or system pkg-config)
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(glfw3  CONFIG REQUIRED)
find_package(GLEW   CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

# ---------------------------------------------------------------------------
#  Sources
#  renderer.cpp lives in engine/ but CMake is typically invoked from the repo
#  root, so we keep the path explicit.
# ---------------------------------------------------------------------------
add_executable(TwilightEngine
    src/renderer.cpp          # VS convention  (copy/symlink from engine/ if needed)
)

# Also support the engine/ layout used by the Makefile
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer.cpp")
    # If src/ doesn't exist fall back to engine/
    set_target_properties(TwilightEngine PROPERTIES
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/engine/renderer.cpp")
endif()

# ---------------------------------------------------------------------------
#  Include directories
# ---------------------------------------------------------------------------
target_include_directories(TwilightEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # stb_image.h lives here
)

# ---------------------------------------------------------------------------
#  Compiler options
# ---------------------------------------------------------------------------
target_compile_definitions(TwilightEngine PRIVATE
    NOMINMAX              # stop windows.h from defining min/max macros
    WIN32_LEAN_AND_MEAN   # trim windows.h bloat
    $<$<CONFIG:Debug>:_DEBUG>
)

if(MSVC)
    target_compile_options(TwilightEngine PRIVATE
        /W3       # reasonable warning level
        /wd4244   # silence float<->double narrowing (common in GL code)
        /wd4305   # silence double->float truncation warnings
        /MP       # multi-processor compilation
    )
else()
    # MinGW / Clang / GCC
    target_compile_options(TwilightEngine PRIVATE -O2 -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
#  Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(TwilightEngine PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
    assimp::assimp
)

# ---------------------------------------------------------------------------
#  Copy shaders/ next to the executable after every build so the
#  runtime path-resolver always finds them via <exe_dir>/shaders/
# ---------------------------------------------------------------------------
add_custom_command(TARGET TwilightEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:TwilightEngine>/shaders"
    COMMENT "Copying shaders/ to output directory"
)

# ---------------------------------------------------------------------------
#  Also copy stb_image.h to include/ if it is missing
#  (avoids a manual download step on fresh clones)
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/stb_image.h")
    message(STATUS "stb_image.h not found — attempting download via CMake")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/stb_image.h"
        STATUS _stb_status
        SHOW_PROGRESS
    )
    list(GET _stb_status 0 _stb_code)
    if(NOT _stb_code EQUAL 0)
        message(WARNING "Could not download stb_image.h. "
                        "Place it manually in include/stb_image.h")
    endif()
endif()
