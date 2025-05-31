# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350 CACHE STRING "Target platform")

cmake_minimum_required(VERSION 3.13)

# Force the build type to Release
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release, Debug, MinSizeRel, RelWithDebInfo)" FORCE)

include(pico_sdk_import.cmake)

project(rp2350_dma_player C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src)

add_executable(rp2350_dma_player
    src/main.c
    src/dma_config.c
    src/sd_loader.c
    hw_config.c
    libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/sd_driver/SPI/sd_spi.c
    libraries/bsp/bsp_cd5300.c
    libraries/bsp/bsp_dma_channel_irq.c
    libraries/bsp/bsp_i2c.c
    libraries/bsp/bsp_ft6146.c
)

target_include_directories(rp2350_dma_player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/sd_driver
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/sd_driver/SPI
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/source
)

target_link_libraries(rp2350_dma_player
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_irq
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)

# Enable stdio over USB and UART
pico_enable_stdio_usb(rp2350_dma_player 1)
pico_enable_stdio_uart(rp2350_dma_player 1)

pico_add_extra_outputs(rp2350_dma_player)

add_definitions(-DENABLE_DEBUG_PRINT) 