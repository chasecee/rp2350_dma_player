#include "bsp_co5300.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"

bsp_co5300_info_t *g_co5300_info;

typedef struct
{
    uint8_t reg;           /*<! The specific LCD command */
    uint8_t *data;         /*<! Buffer that holds the command specific data */
    size_t data_bytes;     /*<! Size of `data` in memory, in bytes */
    unsigned int delay_ms; /*<! Delay in milliseconds after this command */
} bsp_co5300_cmd_t;

void bsp_co5300_tx_cmd(bsp_co5300_cmd_t *cmds, size_t cmd_len)
{
    gpio_put(BSP_CO5300_CS_PIN, 0);
    for (int i = 0; i < cmd_len; i++)
    {
        gpio_put(BSP_CO5300_DC_PIN, 0);
        spi_write_blocking(BSP_CO5300_SPI_NUM, &cmds[i].reg, 1);
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        if (cmds[i].data_bytes > 0)
        {
            gpio_put(BSP_CO5300_DC_PIN, 1);
            spi_write_blocking(BSP_CO5300_SPI_NUM, cmds[i].data, cmds[i].data_bytes);
        }
        if (cmds[i].delay_ms > 0)
        {
            sleep_ms(cmds[i].delay_ms);
        }
    }
    gpio_put(BSP_CO5300_CS_PIN, 1);
}

static void bsp_co5300_spi_init(void)
{
    spi_init(BSP_CO5300_SPI_NUM, 80 * 1000 * 1000); // High speed, adjust if unstable
    gpio_set_function(BSP_CO5300_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(BSP_CO5300_SCLK_PIN, GPIO_FUNC_SPI);

#if BSP_CO5300_MISO_PIN != -1
    gpio_set_function(BSP_CO5300_MISO_PIN, GPIO_FUNC_SPI);
#endif
    // Set SPI mode to Mode 3 (CPOL=1, CPHA=1) as per datasheet timing
    spi_set_format(BSP_CO5300_SPI_NUM, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

// DMA callback: Called when DMA transfer to SPI is complete
void bsp_co5300_dma_callback(void)
{
    // Wait for SPI to finish transmitting everything from its FIFO
    while (spi_get_hw(BSP_CO5300_SPI_NUM)->sr & SPI_SSPSR_BSY_BITS)
        tight_loop_contents(); // or sleep_us(1) if preferred

    // CS is managed by prepare_for_frame_pixels and finish_frame_pixels
    // Do not toggle CS here for partial frame data.

    // Brightness adjustment in ISR is generally not recommended due to potential spi_write_blocking.
    // If brightness needs to be changed, it should ideally be done outside critical paths.
    /*
    if (g_co5300_info->set_brightness_flag)
    {
        g_co5300_info->set_brightness_flag = false;
        // ... brightness setting logic ... (removed for brevity & safety in ISR)
    }
    */

    if (g_co5300_info->dma_flush_done_callback)
    {
        g_co5300_info->dma_flush_done_callback();
    }
}

static void bsp_co5300_spi_dma_init(void)
{
    g_co5300_info->dma_tx_channel = dma_claim_unused_channel(true);
    printf("Display DMA channel claimed: %d\n", g_co5300_info->dma_tx_channel);
    dma_channel_config c = dma_channel_get_default_config(g_co5300_info->dma_tx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8); // Transfer 8-bit units
    channel_config_set_dreq(&c, spi_get_dreq(BSP_CO5300_SPI_NUM, true));
    channel_config_set_read_increment(&c, true);   // Read from incrementing source memory
    channel_config_set_write_increment(&c, false); // Write to fixed SPI data register

    dma_channel_configure(g_co5300_info->dma_tx_channel, &c,
                          &spi_get_hw(BSP_CO5300_SPI_NUM)->dr, // Write address (SPI data register)
                          NULL,                                // Read address (set per transfer)
                          0,                                   // Transfer count (set per transfer)
                          false);                              // Don't start yet

    if (g_co5300_info->dma_flush_done_callback == NULL)
    {
        // It's critical this callback is set for DMA operation
        // Consider a hard fault or error state if not set.
        printf("ERROR: bsp_co5300_spi_dma_init: dma_flush_done_callback is not set!\r\n");
        return;
    }
    // Use the bsp_dma_channel_irq abstraction, assuming DMA_IRQ_0 or DMA_IRQ_1
    // The specific IRQ (0 or 1) depends on SDK and dma_claim_unused_channel behavior
    // This assumes bsp_dma_channel_irq_add can correctly associate the channel with its IRQ handler.
    // For RP2040, each DMA channel can independently trigger an interrupt, handled via shared IRQ lines.
    // Need to ensure bsp_dma_channel_irq.c correctly enables the interrupt for this specific channel.
    dma_channel_set_irq0_enabled(g_co5300_info->dma_tx_channel, true); // Enable IRQ for this channel
    irq_set_exclusive_handler(DMA_IRQ_0, bsp_dma_channel_irq_handler); // Main handler from bsp_dma_channel_irq.c
    irq_set_enabled(DMA_IRQ_0, true);
    bsp_dma_channel_irq_add(0, g_co5300_info->dma_tx_channel, bsp_co5300_dma_callback); // Register our specific handler (FIXED: pass 0, not channel)
}

static void bsp_co5300_gpio_init(void)
{
    gpio_init(BSP_CO5300_DC_PIN);
    gpio_init(BSP_CO5300_CS_PIN);
    gpio_init(BSP_CO5300_RST_PIN);
    gpio_init(BSP_CO5300_PWR_PIN);

    gpio_set_dir(BSP_CO5300_DC_PIN, GPIO_OUT);
    gpio_set_dir(BSP_CO5300_CS_PIN, GPIO_OUT);
    gpio_set_dir(BSP_CO5300_RST_PIN, GPIO_OUT);
    gpio_set_dir(BSP_CO5300_PWR_PIN, GPIO_OUT);
}

static void bsp_co5300_reset(void)
{
    gpio_put(BSP_CO5300_RST_PIN, 0);
    sleep_ms(100);
    gpio_put(BSP_CO5300_RST_PIN, 1);
    sleep_ms(100);
}

static void bsp_co5300_reg_init(void)
{
    bsp_co5300_cmd_t co5300_init_cmds[] = {
        {.reg = 0x11, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 120},     // Sleep Out
        {.reg = 0xc4, .data = (uint8_t[]){0x80}, .data_bytes = 1, .delay_ms = 0},       // Unknown, from original
        {.reg = 0x44, .data = (uint8_t[]){0x01, 0xD7}, .data_bytes = 2, .delay_ms = 0}, // Set Tear Scan Line
        {.reg = 0x35, .data = (uint8_t[]){0x00}, .data_bytes = 1, .delay_ms = 0},       // Tearing Effect Line ON (Mode 0: VBlank)
        {.reg = 0x53, .data = (uint8_t[]){0x20}, .data_bytes = 1, .delay_ms = 10},      // Write CTRL Display (Backlight, Dimming related)
        {.reg = 0x29, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 10},      // Display ON
        {.reg = 0x51, .data = (uint8_t[]){0xA0}, .data_bytes = 1, .delay_ms = 0},       // Write Display Brightness
        //{.reg = 0x20, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 0}, // Display Inversion OFF (already default)
        {.reg = 0x36, .data = (uint8_t[]){0x00}, .data_bytes = 1, .delay_ms = 0}, // MADCTL: MX, MY, MV, ML, BGR, MH, 0, 0 -> 0x00 for standard RGB, top-to-bottom, left-to-right
        {.reg = 0x3A, .data = (uint8_t[]){0x55}, .data_bytes = 1, .delay_ms = 0}, // COLMOD: 0x55 for 16-bit/pixel (RGB565) as per datasheet
    };
    bsp_co5300_tx_cmd(co5300_init_cmds, sizeof(co5300_init_cmds) / sizeof(bsp_co5300_cmd_t));
}

// Prepare display for receiving a stream of pixel data
void bsp_co5300_prepare_for_frame_pixels(void)
{
    gpio_put(BSP_CO5300_CS_PIN, 0); // Assert CS
    gpio_put(BSP_CO5300_DC_PIN, 1); // DC high for data
    // The 0x2C (RAMWR) command is typically sent once before all pixel data by bsp_co5300_set_window
}

// Finish pixel data stream
void bsp_co5300_finish_frame_pixels(void)
{
    // Wait for last DMA transfer to complete via the callback, which itself waits for SPI BSY.
    // Then de-assert CS.
    gpio_put(BSP_CO5300_CS_PIN, 1); // De-assert CS
}

void bsp_co5300_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    bsp_co5300_cmd_t cmds[3];
    uint8_t x_data[4];
    uint8_t y_data[4];

    // Apply offsets
    x_start += g_co5300_info->x_offset;
    x_end += g_co5300_info->x_offset;
    y_start += g_co5300_info->y_offset;
    y_end += g_co5300_info->y_offset;

    x_data[0] = (x_start >> 8) & 0xFF;
    x_data[1] = x_start & 0xFF;
    x_data[2] = (x_end >> 8) & 0xFF;
    x_data[3] = x_end & 0xFF;

    y_data[0] = (y_start >> 8) & 0xFF;
    y_data[1] = y_start & 0xFF;
    y_data[2] = (y_end >> 8) & 0xFF;
    y_data[3] = y_end & 0xFF;

    cmds[0].reg = 0x2a; // CASET
    cmds[0].data = x_data;
    cmds[0].data_bytes = 4;
    cmds[0].delay_ms = 0;

    cmds[1].reg = 0x2b; // RASET
    cmds[1].data = y_data;
    cmds[1].data_bytes = 4;
    cmds[1].delay_ms = 0;

    cmds[2].reg = 0x2c; // RAMWR (Memory Write)
    cmds[2].data = NULL;
    cmds[2].data_bytes = 0;
    cmds[2].delay_ms = 0;

    bsp_co5300_tx_cmd(cmds, 3);
}

// Flush pixel data to the display (assumes CS and DC are already set appropriately by prepare_for_frame_pixels)
void bsp_co5300_flush(uint8_t *color_data, size_t num_bytes)
{
    if (g_co5300_info->enabled_dma)
    {
        // Ensure DMA is not already busy (should be handled by dma_transfer_complete flag in main.c)
        // dma_channel_is_busy(g_co5300_info->dma_tx_channel)

        // CS and DC are managed by prepare_for_frame_pixels and finish_frame_pixels
        dma_channel_set_read_addr(g_co5300_info->dma_tx_channel, color_data, false); // Set read address, don't trigger yet
        dma_channel_set_trans_count(g_co5300_info->dma_tx_channel, num_bytes, true); // Set transfer count and trigger
    }
    else
    {
        // CS and DC are managed by prepare_for_frame_pixels and finish_frame_pixels
        spi_write_blocking(BSP_CO5300_SPI_NUM, color_data, num_bytes);
        // For blocking SPI, the user callback should be called directly after this if it were the only way to signal completion.
        // However, with the dma_transfer_complete flag in main.c, this might not be needed here.
        // if (g_co5300_info->dma_flush_done_callback) g_co5300_info->dma_flush_done_callback();
    }
}

void bsp_co5300_set_brightness(uint8_t brightness)
{
    g_co5300_info->brightness = brightness;
    // Defer brightness change if DMA is active to avoid spi_write_blocking in ISR or critical sections.
    // A flag can be set, and main loop can apply it when safe.
    // For simplicity here, apply directly. Consider implications if called frequently during active DMA.
    if (!dma_channel_is_busy(g_co5300_info->dma_tx_channel) || !g_co5300_info->enabled_dma)
    {
        bsp_co5300_cmd_t cmd;
        // Brightness scaling: 0x25 (min) to 0xFF (max) according to some drivers, actual range might vary.
        // Datasheet mentions WRDISBV (51h) takes one param 00h-FFh.
        uint8_t cmd_data = brightness; // Assuming brightness is 0-255 directly for now.
                                       // Original: 0x25 + g_co5300_info->brightness * (0xFF - 0x25) / 100;
        cmd.reg = 0x51;                // WRDISBV
        cmd.data = &cmd_data;
        cmd.data_bytes = 1;
        cmd.delay_ms = 0;
        bsp_co5300_tx_cmd(&cmd, 1);
    }
    else
    {
        g_co5300_info->set_brightness_flag = true; // Request brightness change after DMA
    }
}

void bsp_co5300_set_power(bool on)
{
    g_co5300_info->power_on = on;
    gpio_put(BSP_CO5300_PWR_PIN, on);
}

bsp_co5300_info_t *bsp_co5300_get_info(void)
{
    return g_co5300_info;
}

void bsp_co5300_init(bsp_co5300_info_t *co5300_info)
{
    g_co5300_info = co5300_info;

    bsp_co5300_gpio_init();
    bsp_co5300_spi_init(); // Init SPI before power & reset, as some pins might be shared or need early config
    bsp_co5300_set_power(true);
    bsp_co5300_reset();
    bsp_co5300_reg_init(); // Send initialization commands

    if (co5300_info->enabled_dma)
    {
        bsp_co5300_spi_dma_init();
    }
    // Set initial brightness
    bsp_co5300_set_brightness(co5300_info->brightness);
    // Initial window set is usually done by main application before first flush
}