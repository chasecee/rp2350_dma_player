#include "hw_config.h"   // From the SD card library, declares sd_get_num, sd_get_by_num
#include "pico/stdlib.h" // For spi0 definition

/* Configuration of hardware SPI object */
static spi_t spi = {
    .hw_inst = spi0, // SPI component
    .sck_gpio = 2,   // GPIO number (not Pico pin number)
    .mosi_gpio = 3,
    .miso_gpio = 4,
    .baud_rate = 150 * 1000 * 1000 / 4, // 37.5 MHz - more stable than 50MHz
    .spi_mode = 3,                      // SPI Mode 3 for better stability
    .set_drive_strength = true,
    .sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
    .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
    .use_static_dma_channels = true, // Enable DMA
    .tx_dma = 2,                     // DMA channel for TX
    .rx_dma = 3                      // DMA channel for RX
};

/* SPI Interface */
static sd_spi_if_t spi_if = {
    .spi = &spi,  // Pointer to the SPI driving this card
    .ss_gpio = 5, // The SPI slave select GPIO for this SD card
    .set_drive_strength = true,
    .ss_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA};

/* Configuration of the SD Card socket object */
sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if,     // Pointer to the SPI interface driving this card
    .use_card_detect = false // No card detect pin in use
};

/* ********************************************************************** */

// Implementation of functions required by the SD card library.
// These are declared in the library's sd_driver/hw_config.h.

size_t sd_get_num()
{
    return 1; // We've configured one SD card
}

sd_card_t *sd_get_by_num(size_t num)
{
    if (0 == num)
    {
        return &sd_card; // Return our configured sd_card_t instance
    }
    return NULL; // No other cards configured
}