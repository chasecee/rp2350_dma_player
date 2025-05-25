#include "hw_config.h"   // From the SD card library, declares sd_get_num, sd_get_by_num
#include "pico/stdlib.h" // For spi0 definition

/* Configuration of hardware SPI object */
static spi_t spi = {
    .hw_inst = spi0, // SPI component
    .sck_gpio = 2,   // GPIO number (not Pico pin number)
    .mosi_gpio = 3,
    .miso_gpio = 4,
    .baud_rate = 125 * 1000 * 1000 / 2, // Increased to 62.5 MHz
    .spi_mode = 3                       // Explicitly set to SPI Mode 3
    // Other spi_t fields will be default (0/false) if not specified
};

/* SPI Interface */
static sd_spi_if_t spi_if = {
    .spi = &spi, // Pointer to the SPI driving this card
    .ss_gpio = 5 // The SPI slave select GPIO for this SD card
    // Other sd_spi_if_t fields will be default (0/false)
};

/* Configuration of the SD Card socket object */
// This is the global sd_card_t instance that sd_get_by_num will return.
sd_card_t sd_card = {
    // Changed to non-static to match previous hw_config.c,
    // ensuring it's globally accessible if anything else relies on that.
    // If only used by sd_get_by_num, static is fine.
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if // Pointer to the SPI interface driving this card
    // Card detection is not configured here.
    // To enable it, you would set:
    // .use_card_detect = true,
    // .card_detect_gpio = YOUR_CD_GPIO,
    // .card_detected_true = 1 (or 0, depending on your CD switch)
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