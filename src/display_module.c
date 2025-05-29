#include "display_module.h"
#include "pico/stdlib.h" // For sleep_us
#include <stdio.h>       // For printf (optional, for debugging)

// DMA line buffers (for display)
static uint16_t frame_line_buffer_a[DISPLAY_WIDTH];
static uint16_t frame_line_buffer_b[DISPLAY_WIDTH];
volatile bool g_dma_transfer_complete = true; // Moved from main.c

void display_module_dma_done_callback(void)
{
    g_dma_transfer_complete = true; // Signal DMA completion
}

void display_module_init(channel_irq_callback_t dma_callback)
{
    printf("Initializing display module...\n");
    bsp_co5300_info_t display_info = {
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
        .x_offset = 6,
        .y_offset = 0,
        .brightness = 80,
        .enabled_dma = true,
        .dma_flush_done_callback = dma_callback // Use the callback from main
    };
    bsp_co5300_init(&display_info);
    printf("Display module initialized.\n");
}

void display_module_render_frame(const uint16_t *frame_buffer_data)
{
    // printf("Rendering frame...\n");
    bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

    volatile uint16_t *cpu_buffer_ptr = frame_line_buffer_a;
    volatile uint16_t *dma_buffer_ptr = frame_line_buffer_b;

    // Prime the first buffer for DMA (display line 0)
    int sy_scaled = (0 * FRAME_HEIGHT) / DISPLAY_HEIGHT;
    for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
    {
        int sx_scaled = (dx * FRAME_WIDTH) / DISPLAY_WIDTH;
        ((uint16_t *)cpu_buffer_ptr)[dx] = frame_buffer_data[sy_scaled * FRAME_WIDTH + sx_scaled];
    }

    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        volatile uint16_t *temp_buf = dma_buffer_ptr;
        dma_buffer_ptr = cpu_buffer_ptr;
        cpu_buffer_ptr = temp_buf;

        while (!g_dma_transfer_complete)
        {
            sleep_us(10);
        }
        g_dma_transfer_complete = false;
        bsp_co5300_flush((uint16_t *)dma_buffer_ptr, DISPLAY_WIDTH);

        if (y < DISPLAY_HEIGHT - 1)
        {
            int display_y_next = y + 1;
            sy_scaled = (display_y_next * FRAME_HEIGHT) / DISPLAY_HEIGHT;
            for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
            {
                int sx_scaled = (dx * FRAME_WIDTH) / DISPLAY_WIDTH;
                ((uint16_t *)cpu_buffer_ptr)[dx] = frame_buffer_data[sy_scaled * FRAME_WIDTH + sx_scaled];
            }
        }
    }

    while (!g_dma_transfer_complete)
    {
        sleep_us(10);
    }
    // printf("Frame rendering complete.\n");
}