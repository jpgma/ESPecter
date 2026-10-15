set(PR32 "${CMAKE_SOURCE_DIR}/third_party/pixelroot32")
set(APU "${CMAKE_SOURCE_DIR}/third_party/pixelroot32-apu")

if(NOT EXISTS "${PR32}/include/core/Engine.h")
    message(FATAL_ERROR "PixelRoot32 is missing. Run: git submodule update --init")
endif()
if(NOT EXISTS "${APU}/include/pixelroot32/apu/ApuCore.h")
    message(FATAL_ERROR "PixelRoot32-APU is missing. Run: git submodule update --init")
endif()

include("${CMAKE_SOURCE_DIR}/cmake/pixelroot32.cmake")
especter_pixelroot32_sources(ENGINE_SRCS "${PR32}" OFF)

idf_component_register(
    SRCS
        "app_main.c"
        "${CMAKE_SOURCE_DIR}/game/arena.c"
        "${CMAKE_SOURCE_DIR}/game/actions.c"
        "${CMAKE_SOURCE_DIR}/game/world.c"
        "${CMAKE_SOURCE_DIR}/host/engine_host.cpp"
        "${CMAKE_SOURCE_DIR}/host/st7789_drawer.cpp"
        ${ENGINE_SRCS}
    INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/game"
        "${CMAKE_SOURCE_DIR}/host"
        "${CMAKE_SOURCE_DIR}/compat"
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
    "SHELL:-include ${CMAKE_SOURCE_DIR}/host/pr32_config.h"
    "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
)
