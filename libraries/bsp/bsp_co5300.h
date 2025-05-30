#ifndef __BSP_CO5300_H__
#define __BSP_CO5300_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "bsp_dma_channel_irq.h"

#define BSP_CO5300_SPI_NUM spi1

#define BSP_CO5300_MOSI_PIN 11
#define BSP_CO5300_MISO_PIN -1
#define BSP_CO5300_SCLK_PIN 10

#define BSP_CO5300_DC_PIN 12
#define BSP_CO5300_CS_PIN 13
#define BSP_CO5300_RST_PIN 14

#define BSP_CO5300_PWR_PIN 15

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;

    uint8_t brightness;

    uint dma_tx_channel;

    bool set_brightness_flag;
    bool enabled_dma;

    bool power_on;

    channel_irq_callback_t dma_flush_done_callback;
} bsp_co5300_info_t;

bsp_co5300_info_t *bsp_co5300_get_info(void);

void bsp_co5300_init(bsp_co5300_info_t *co5300_info);
void bsp_co5300_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);

/**
 * @brief Flushes a buffer of color data to the display via SPI (DMA or blocking).
 * Assumes CS and DC lines are managed externally for pixel data if part of a larger frame write.
 *
 * @param color_data Pointer to the color data buffer (now uint8_t* for 8-bit pixels).
 * @param num_pixels Number of pixels to flush (each pixel is 1 byte for 8-bit mode).
 */
void bsp_co5300_flush(uint8_t *color_data, size_t num_pixels);

void bsp_co5300_set_brightness(uint8_t brightness);
void bsp_co5300_set_power(bool on);

void bsp_co5300_prepare_for_frame_pixels(void);
void bsp_co5300_finish_frame_pixels(void);

#endif // __BSP_CO5300_H__