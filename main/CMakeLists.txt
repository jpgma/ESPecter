# Early expansion runs cmake -P with the working directory set to build, so CMAKE_SOURCE_DIR is not the repo root.
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(PR32 "${ROOT}/third_party/pixelroot32")
set(APU "${ROOT}/third_party/pixelroot32-apu")

if(NOT EXISTS "${PR32}/include/core/Engine.h")
    message(FATAL_ERROR "PixelRoot32 is missing. Run: git submodule update --init")
endif()
if(NOT EXISTS "${APU}/include/pixelroot32/apu/ApuCore.h")
    message(FATAL_ERROR "PixelRoot32-APU is missing. Run: git submodule update --init")
endif()

include("${ROOT}/cmake/pixelroot32.cmake")
especter_pixelroot32_sources(ENGINE_SRCS "${PR32}" OFF)

idf_component_register(
    SRCS
        "app_main.c"
        "${ROOT}/game/arena.c"
        "${ROOT}/game/actions.c"
        "${ROOT}/game/world.c"
        "${ROOT}/host/engine_host.cpp"
        "${ROOT}/host/st7789_drawer.cpp"
        ${ENGINE_SRCS}
    INCLUDE_DIRS
        "${ROOT}/game"
        "${ROOT}/host"
        "${ROOT}/compat"
        "${PR32}/include"
        "${PR32}/src"
        "${APU}/include"
    REQUIRES
        esp_lcd
        driver
        esp_timer
        bt
        nvs_flash
        esp_hw_support
        esp_rom
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    "SHELL:-include ${ROOT}/host/pr32_config.h"
    "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
)
