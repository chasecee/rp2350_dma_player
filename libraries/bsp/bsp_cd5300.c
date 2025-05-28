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
        // printf("delay_ms:%d\r\n", cmds[i].delay_ms);
        if (cmds[i].delay_ms > 0)
        {
            // printf("delay_ms:%d\r\n", cmds[i].delay_ms);
            sleep_ms(cmds[i].delay_ms);
        }
    }
    gpio_put(BSP_CO5300_CS_PIN, 1);
}

static void bsp_co5300_spi_init(void)
{
    spi_init(BSP_CO5300_SPI_NUM, 80 * 1000 * 1000); // Original 80 MHz
    // spi_init(BSP_CO5300_SPI_NUM, 50 * 1000 * 1000); // Changed to 50 MHz (datasheet max)
    gpio_set_function(BSP_CO5300_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(BSP_CO5300_SCLK_PIN, GPIO_FUNC_SPI);

#if BSP_CO5300_MISO_PIN != -1
    gpio_set_function(BSP_CO5300_MISO_PIN, GPIO_FUNC_SPI);
#endif
    // 设置spi的模式
    spi_set_format(BSP_CO5300_SPI_NUM, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void bsp_co5300_dma_callback(void)
{
    // Wait for SPI to finish transmitting everything from its FIFO
    while (spi_get_hw(BSP_CO5300_SPI_NUM)->sr & SPI_SSPSR_BSY_BITS)
        ;

    // sleep_us(1); // Temporarily commenting out sleep_us as well
    gpio_put(BSP_CO5300_CS_PIN, 1);
    /* Temporarily comment out brightness adjustment in ISR
    if (g_co5300_info->set_brightness_flag)
    {
        g_co5300_info->set_brightness_flag = false;
        bsp_co5300_cmd_t cmd;
        uint8_t cmd_data = 0x25 + g_co5300_info->brightness * (0xFF - 0x25) / 100;
        cmd.reg = 0x51;
        cmd.data = &cmd_data;
        cmd.data_bytes = 1;
        cmd.delay_ms = 0;
        bsp_co5300_tx_cmd(&cmd, 1);
        cmd.reg = 0x2c;
        cmd.data = NULL;
        cmd.data_bytes = 0;
        cmd.delay_ms = 0;
        bsp_co5300_tx_cmd(&cmd, 1);
    }
    */
    g_co5300_info->dma_flush_done_callback();
}

static void bsp_co5300_spi_dma_init(void)
{
    g_co5300_info->dma_tx_channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(g_co5300_info->dma_tx_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(BSP_CO5300_SPI_NUM, true));
    dma_channel_configure(g_co5300_info->dma_tx_channel, &c,
                          &spi_get_hw(BSP_CO5300_SPI_NUM)->dr, // write address
                          NULL,                                // read address
                          0,                                   // element count (each element is of size transfer_data_size)
                          false);                              // don't start yet

    if (g_co5300_info->dma_flush_done_callback == NULL)
    {
        printf("dma_flush_done_callback is not set\r\n");
        return;
    }
    bsp_dma_channel_irq_add(1, g_co5300_info->dma_tx_channel, bsp_co5300_dma_callback);
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
        //  {cmd, { data }, data_size, delay_ms}

        {.reg = 0x11, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 120},
        // {.reg = 0xff, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 120},
        // {.reg = 0x3b, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 10},
        {.reg = 0xc4, .data = (uint8_t[]){0x80}, .data_bytes = 1, .delay_ms = 0},
        {.reg = 0x44, .data = (uint8_t[]){0x01, 0xD7}, .data_bytes = 2, .delay_ms = 0},
        {.reg = 0x35, .data = (uint8_t[]){0x00}, .data_bytes = 1, .delay_ms = 0},
        {.reg = 0x53, .data = (uint8_t[]){0x20}, .data_bytes = 1, .delay_ms = 10},
        {.reg = 0x29, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 10},
        {.reg = 0x51, .data = (uint8_t[]){0xA0}, .data_bytes = 1, .delay_ms = 0},
        {.reg = 0x20, .data = (uint8_t[]){0x00}, .data_bytes = 0, .delay_ms = 0},
        {.reg = 0x36, .data = (uint8_t[]){0x00}, .data_bytes = 1, 0},
        {.reg = 0x3A, .data = (uint8_t[]){0x02}, .data_bytes = 1, .delay_ms = 0},
    };

    bsp_co5300_tx_cmd(co5300_init_cmds, sizeof(co5300_init_cmds) / sizeof(bsp_co5300_cmd_t));
}

void bsp_co5300_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    bsp_co5300_cmd_t cmds[3];
    uint8_t x_data[4];
    uint8_t y_data[4];
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

    cmds[0].reg = 0x2a;
    cmds[0].data = x_data;
    cmds[0].data_bytes = 4;
    cmds[0].delay_ms = 0;

    cmds[1].reg = 0x2b;
    cmds[1].data = y_data;
    cmds[1].data_bytes = 4;
    cmds[1].delay_ms = 0;

    cmds[2].reg = 0x2c;
    cmds[2].data = NULL;
    cmds[2].data_bytes = 0;
    cmds[2].delay_ms = 0;

    bsp_co5300_tx_cmd(cmds, 3);
}

void bsp_co5300_flush(uint8_t *color, size_t color_len)
{
    // static uint32_t flush_pixel_sum = 0;
    // flush_pixel_sum += color_len;

    // if (flush_pixel_sum >= g_co5300_info->width * g_co5300_info->height)
    // {
    //     flush_pixel_sum = 0;
    //     bsp_co5300_cmd_t cmd;
    //     cmd.reg = 0x2c;
    //     cmd.data = NULL;
    //     cmd.data_bytes = 0;
    //     cmd.delay_ms = 0;
    //     bsp_co5300_tx_cmd(&cmd, 1);
    // }

    if (g_co5300_info->enabled_dma)
    {
        gpio_put(BSP_CO5300_CS_PIN, 0);
        gpio_put(BSP_CO5300_DC_PIN, 1);
        dma_channel_set_trans_count(g_co5300_info->dma_tx_channel, color_len, true);
        dma_channel_set_read_addr(g_co5300_info->dma_tx_channel, color, false);
    }
    else
    {
        gpio_put(BSP_CO5300_CS_PIN, 0);
        gpio_put(BSP_CO5300_DC_PIN, 1);
        spi_write_blocking(BSP_CO5300_SPI_NUM, (uint8_t *)color, color_len);
        gpio_put(BSP_CO5300_CS_PIN, 1);
    }
}

void bsp_co5300_set_brightness(uint8_t brightness)
{
    g_co5300_info->brightness = brightness;
    if (g_co5300_info->enabled_dma)
    {
        g_co5300_info->set_brightness_flag = true;
    }
    else
    {
        bsp_co5300_cmd_t cmd;
        uint8_t cmd_data = 0x25 + brightness * (0xFF - 0x25) / 100;
        cmd.reg = 0x51;
        cmd.data = &cmd_data;
        cmd.data_bytes = 1;
        cmd.delay_ms = 0;
        bsp_co5300_tx_cmd(&cmd, 1);
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

    bsp_co5300_spi_init();
    bsp_co5300_gpio_init();
    bsp_co5300_set_power(true);
    bsp_co5300_reset();
    bsp_co5300_reg_init();
    // bsp_co5300_set_window(0, 0, co5300_info->width - 1, co5300_info->height - 1);
    if (co5300_info->enabled_dma)
    {
        bsp_co5300_spi_dma_init();
    }
    bsp_co5300_set_brightness(co5300_info->brightness);
}