#include "display_native.h"
#include "bsp_co5300.h"
#include "debug.h"
#include <string.h>

// Internal state
static display_native_config_t config;
static bool initialized = false;

// Static buffer for native display operations
#define LINES_PER_DMA 233
static uint16_t multi_line_buffer[466 * LINES_PER_DMA] __attribute__((aligned(32))); // Max possible width

bool display_native_init(const display_native_config_t *cfg)
{
    if (!cfg || !cfg->dma_complete_flag)
    {
        return false;
    }

    config = *cfg;
    initialized = true;

    DBG_PRINTF("DISPLAY_NATIVE: Initialized %dx%d centered on %dx%d\n",
               config.frame_width, config.frame_height,
               config.physical_width, config.physical_height);

    return true;
}

void display_native_show_frame(const uint16_t *frame_buffer)
{
    if (!initialized || !frame_buffer)
    {
        return;
    }

    // Calculate centering offsets
    const int x_offset = (config.physical_width - config.frame_width) / 2;
    const int y_offset = (config.physical_height - config.frame_height) / 2;

    DBG_PRINTF("DISPLAY_NATIVE: Showing frame (centered at %d,%d)\n", x_offset, y_offset);

    // Set display window to centered region
    bsp_co5300_set_window(x_offset, y_offset,
                          x_offset + config.frame_width - 1,
                          y_offset + config.frame_height - 1);
    bsp_co5300_prepare_for_frame_pixels();

    // Process all lines at once for native mode (no batching needed)
    int lines_processed = 0;

    for (int batch_start = 0; batch_start < config.frame_height; batch_start += LINES_PER_DMA)
    {
        int lines_in_batch = LINES_PER_DMA;
        if (batch_start + lines_in_batch > config.frame_height)
            lines_in_batch = config.frame_height - batch_start;

        // Clear the multi-line buffer to prevent stale data artifacts
        memset(multi_line_buffer, 0, config.frame_width * LINES_PER_DMA * sizeof(uint16_t));

        // Copy lines from frame buffer to multi-line buffer
        for (int line = 0; line < lines_in_batch; line++)
        {
            int src_y = batch_start + line;

            // Bounds checking - ensure we don't read past frame boundaries
            if (src_y >= config.frame_height)
            {
                break;
            }

            const uint16_t *src_line = &frame_buffer[src_y * config.frame_width];
            uint16_t *dst_line = &multi_line_buffer[line * config.frame_width];

            // Direct copy - no scaling needed
            for (int x = 0; x < config.frame_width; x++)
            {
                dst_line[x] = src_line[x];
            }
        }

        // Send this batch via DMA - only send the actual lines needed
        while (!*config.dma_complete_flag)
            __asm volatile("nop");
        *config.dma_complete_flag = false;

        bsp_co5300_flush((uint8_t *)multi_line_buffer,
                         config.frame_width * lines_in_batch * sizeof(uint16_t));
        lines_processed += lines_in_batch;
    }

    // Wait for the last DMA transfer to complete
    while (!*config.dma_complete_flag)
        __asm volatile("nop");

    bsp_co5300_finish_frame_pixels();

    DBG_PRINTF("DISPLAY_NATIVE: Frame complete (%d lines)\n", lines_processed);
}

bool display_native_is_ready(void)
{
    if (!initialized)
    {
        return false;
    }

    return *config.dma_complete_flag;
}