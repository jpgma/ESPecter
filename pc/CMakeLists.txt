cmake_minimum_required(VERSION 3.16)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
project(especter_pc C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(PR32 "${ROOT}/third_party/pixelroot32")
set(APU "${ROOT}/third_party/pixelroot32-apu")

if(NOT EXISTS "${PR32}/include/core/Engine.h")
    message(FATAL_ERROR "PixelRoot32 sources are missing from third_party/pixelroot32")
endif()
if(NOT EXISTS "${APU}/include/pixelroot32/apu/ApuCore.h")
    message(FATAL_ERROR "PixelRoot32-APU sources are missing from third_party/pixelroot32-apu")
endif()

include("${ROOT}/cmake/pixelroot32.cmake")
especter_pixelroot32_sources(ENGINE_SRCS "${PR32}" ON)

include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.10
    GIT_SHALLOW TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

set(SDL2_PREFIX "${CMAKE_BINARY_DIR}/sdl2_prefixed/SDL2")
file(MAKE_DIRECTORY "${SDL2_PREFIX}")
file(COPY "${SDL2_SOURCE_DIR}/include/" DESTINATION "${SDL2_PREFIX}")
if(EXISTS "${SDL2_BINARY_DIR}/include/SDL_config.h")
    file(COPY "${SDL2_BINARY_DIR}/include/SDL_config.h" DESTINATION "${SDL2_PREFIX}")
elseif(EXISTS "${SDL2_BINARY_DIR}/include-config-rel/SDL2/SDL_config.h")
    file(COPY "${SDL2_BINARY_DIR}/include-config-rel/SDL2/SDL_config.h" DESTINATION "${SDL2_PREFIX}")
elseif(EXISTS "${SDL2_BINARY_DIR}/include-config-/SDL2/SDL_config.h")
    file(COPY "${SDL2_BINARY_DIR}/include-config-/SDL2/SDL_config.h" DESTINATION "${SDL2_PREFIX}")
endif()

add_executable(especter_pc
    main.c
    actions_ds4.c
    "${ROOT}/game/arena.c"
    "${ROOT}/game/body_sheet.c"
    "${ROOT}/game/palette.c"
    "${ROOT}/game/world.c"
    "${ROOT}/host/engine_host.cpp"
    ${ENGINE_SRCS}
)

target_include_directories(especter_pc PRIVATE
    "${CMAKE_BINARY_DIR}/sdl2_prefixed"
    "${SDL2_BINARY_DIR}/include"
    "${ROOT}/game"
    "${ROOT}/host"
    "${PR32}/include"
    "${PR32}/src"
    "${APU}/include"
)

target_compile_definitions(especter_pc PRIVATE
    PLATFORM_NATIVE
    SDL_MAIN_HANDLED
)

if(MSVC)
    target_compile_options(especter_pc PRIVATE
        "/FI${ROOT}/host/pr32_config.h"
        $<$<COMPILE_LANGUAGE:CXX>:/std:c++17>
    )
    target_compile_definitions(especter_pc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(especter_pc PRIVATE
        "SHELL:-include ${ROOT}/host/pr32_config.h"
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    )
endif()

if(TARGET SDL2::SDL2-static)
    target_link_libraries(especter_pc PRIVATE SDL2::SDL2-static)
else()
    target_link_libraries(especter_pc PRIVATE SDL2::SDL2)
endif()

if(WIN32)
    target_link_libraries(especter_pc PRIVATE winmm imm32 version setupapi)
endif()
