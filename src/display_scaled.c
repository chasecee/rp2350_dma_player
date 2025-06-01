#include "display_scaled.h"
#include "bsp_co5300.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

// Internal state
static display_scaled_config_t config;
static bool initialized = false;

// Precomputed scaling optimizations for 2x2 scaling
static uint16_t *src_line_lookup = NULL;
static bool *line_is_duplicate = NULL;

// Display buffers for batched processing
#define SCALED_LINES_PER_BATCH 16                                                               // Conservative batch size to avoid conflicts
static uint16_t display_line_buffer[466 * SCALED_LINES_PER_BATCH] __attribute__((aligned(32))); // Max possible width
static uint16_t last_line_buffer[466] __attribute__((aligned(32)));                             // For cross-batch duplicate handling

// Function pointer for loader cooperation
static void (*loader_yield_fn)(void) = NULL;

// Optimized 2x horizontal scaling using direct memory operations
static inline void scale_line_2x_optimized(const uint16_t *src_line, uint16_t *dst_line)
{
    int src_x = 0;
    int dst_x = 0;

    // Main loop: process 4 pixels at a time (unrolled for speed)
    for (; src_x < (config.frame_width & ~3); src_x += 4, dst_x += 8)
    {
        uint16_t p0 = src_line[src_x];
        uint16_t p1 = src_line[src_x + 1];
        uint16_t p2 = src_line[src_x + 2];
        uint16_t p3 = src_line[src_x + 3];

        // Write 8 destination pixels (2x scaling)
        dst_line[dst_x] = p0;
        dst_line[dst_x + 1] = p0;
        dst_line[dst_x + 2] = p1;
        dst_line[dst_x + 3] = p1;
        dst_line[dst_x + 4] = p2;
        dst_line[dst_x + 5] = p2;
        dst_line[dst_x + 6] = p3;
        dst_line[dst_x + 7] = p3;
    }

    // Handle remaining pixels (up to 3)
    for (; src_x < config.frame_width; src_x++, dst_x += 2)
    {
        uint16_t pixel = src_line[src_x];
        dst_line[dst_x] = pixel;
        if (dst_x + 1 < config.physical_width)
            dst_line[dst_x + 1] = pixel;
    }
}

bool display_scaled_init(const display_scaled_config_t *cfg)
{
    if (!cfg || !cfg->dma_complete_flag)
    {
        return false;
    }

    config = *cfg;

    // Allocate and initialize scaling lookup tables
    src_line_lookup = malloc(config.physical_height * sizeof(uint16_t));
    line_is_duplicate = malloc(config.physical_height * sizeof(bool));

    if (!src_line_lookup || !line_is_duplicate)
    {
        free(src_line_lookup);
        free(line_is_duplicate);
        return false;
    }

    // Precompute scaling lookup tables
    for (int dst_y = 0; dst_y < config.physical_height; dst_y++)
    {
        src_line_lookup[dst_y] = dst_y / 2;          // Precompute source line mapping
        line_is_duplicate[dst_y] = (dst_y % 2 == 1); // Every odd line is a duplicate
    }

    initialized = true;

    DBG_PRINTF("DISPLAY_SCALED: Initialized %dx%d -> %dx%d scaling\n",
               config.frame_width, config.frame_height,
               config.physical_width, config.physical_height);
    DBG_PRINTF("DISPLAY_SCALED: Using %d lines per batch\n", SCALED_LINES_PER_BATCH);

    return true;
}

void display_scaled_show_frame(const uint16_t *frame_buffer)
{
    if (!initialized || !frame_buffer)
    {
        return;
    }

    DBG_PRINTF("DISPLAY_SCALED: Showing scaled frame\n");

    // Set up display window once for the entire frame
    bsp_co5300_set_window(0, 0, config.physical_width - 1, config.physical_height - 1);
    bsp_co5300_prepare_for_frame_pixels();

    // Process frame in batches to conserve memory
    for (int batch_start = 0; batch_start < config.physical_height; batch_start += SCALED_LINES_PER_BATCH)
    {
        int lines_in_batch = SCALED_LINES_PER_BATCH;
        if (batch_start + lines_in_batch > config.physical_height)
            lines_in_batch = config.physical_height - batch_start;

        // Clear the batch buffer
        memset(display_line_buffer, 0, config.physical_width * lines_in_batch * sizeof(uint16_t));

        // FIXED: Proper duplicate line handling across batches
        for (int batch_line = 0; batch_line < lines_in_batch; batch_line++)
        {
            int dst_y = batch_start + batch_line;
            uint16_t *dst_line = &display_line_buffer[batch_line * config.physical_width];

            // Use precomputed lookup instead of division
            uint16_t src_y = src_line_lookup[dst_y];

            if (src_y < config.frame_height)
            {
                if (line_is_duplicate[dst_y])
                {
                    // This is a duplicate line - copy from the correct previous line
                    if (batch_line > 0)
                    {
                        // Previous line is in current batch
                        uint16_t *prev_line = &display_line_buffer[(batch_line - 1) * config.physical_width];
                        memcpy(dst_line, prev_line, config.physical_width * sizeof(uint16_t));
                    }
                    else
                    {
                        // Previous line was in previous batch - use saved line
                        memcpy(dst_line, last_line_buffer, config.physical_width * sizeof(uint16_t));
                    }
                }
                else
                {
                    // Scale this source line using optimized function
                    const uint16_t *src_line = &frame_buffer[src_y * config.frame_width];
                    scale_line_2x_optimized(src_line, dst_line);
                }
            }
            // If src_y >= frame_height, line remains black (from memset)
        }

        // Before sending this batch, save the last line for next batch's duplicate handling
        if (lines_in_batch > 0)
        {
            uint16_t *last_line_in_batch = &display_line_buffer[(lines_in_batch - 1) * config.physical_width];
            memcpy(last_line_buffer, last_line_in_batch, config.physical_width * sizeof(uint16_t));
        }

        // Send this batch - continuous stream, no window reset
        while (!*config.dma_complete_flag)
            __asm volatile("nop");

        // Extra safety: ensure any pending SD DMA is also complete before starting display DMA
        __asm volatile("dmb" ::: "memory"); // Memory barrier to ensure all writes are complete

        *config.dma_complete_flag = false;
        bsp_co5300_flush((uint8_t *)display_line_buffer,
                         config.physical_width * lines_in_batch * sizeof(uint16_t));

        // DISABLED: Don't yield to SD loader during display - causes choppiness!
        // Frame display should be atomic for smooth playback
        /*
        if (loader_yield_fn)
        {
            loader_yield_fn();
        }
        */
    }

    // Wait for the last DMA transfer to complete
    while (!*config.dma_complete_flag)
        __asm volatile("nop");

    bsp_co5300_finish_frame_pixels();

    DBG_PRINTF("DISPLAY_SCALED: Scaled frame complete\n");
}

bool display_scaled_is_ready(void)
{
    if (!initialized)
    {
        return false;
    }

    return *config.dma_complete_flag;
}

void display_scaled_yield_to_loader(void)
{
    // Set the yield function for cooperative multitasking
    extern void raw_sd_loader_process(void);
    loader_yield_fn = raw_sd_loader_process;
}