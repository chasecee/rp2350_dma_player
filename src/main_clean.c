#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "diskio.h"
#include "sd_card.h"
#include "bsp_co5300.h"
#include "display_manager.h"
#include "frame_loader.h"
#include "debug.h"

// Configuration
#define PHYSICAL_WIDTH 466
#define PHYSICAL_HEIGHT 466
#define FRAME_WIDTH 233
#define FRAME_HEIGHT 233
#define TOTAL_FRAMES 3403

// Choose your display mode here
#define USE_SCALED_MODE 1 // 0 = native centered, 1 = scaled fullscreen

// Timing
#define TARGET_FPS 30
#define TARGET_FRAME_US (1000000 / TARGET_FPS)

volatile bool dma_transfer_complete = true;

void dma_done_callback(void)
{
    dma_transfer_complete = true;
}

int main()
{
    printf("RP2350 DMA Player Starting...\n");

    // Basic setup
    stdio_init_all();
    set_sys_clock_khz(150000, true);
    sleep_ms(100);
    DBG_PRINTF("System clock: %lu Hz\n", clock_get_hz(clk_sys));

    // Initialize display hardware
    bsp_co5300_info_t display_info = {
        .width = PHYSICAL_WIDTH, .height = PHYSICAL_HEIGHT, .x_offset = 6, .y_offset = 0, .brightness = 95, .enabled_dma = true, .dma_flush_done_callback = dma_done_callback};
    bsp_co5300_init(&display_info);

    // Initialize SD card
    if (!sd_init_driver() || disk_initialize(0) != 0)
    {
        printf("SD card initialization failed!\n");
        while (1)
            tight_loop_contents();
    }

    // Initialize modular components
    display_config_t disp_cfg = {
        .mode = USE_SCALED_MODE ? DISPLAY_MODE_SCALED : DISPLAY_MODE_NATIVE,
        .physical_width = PHYSICAL_WIDTH,
        .physical_height = PHYSICAL_HEIGHT,
        .frame_width = FRAME_WIDTH,
        .frame_height = FRAME_HEIGHT,
        .dma_complete_flag = &dma_transfer_complete};

    frame_loader_config_t loader_cfg = {
        .total_frames = TOTAL_FRAMES,
        .frame_width = FRAME_WIDTH,
        .frame_height = FRAME_HEIGHT};

    if (!display_manager_init(&disp_cfg) || !frame_loader_init(&loader_cfg))
    {
        printf("Module initialization failed!\n");
        while (1)
            tight_loop_contents();
    }

    DBG_PRINTF("All modules initialized. Starting animation loop...\n");

    // Main animation loop
    int current_frame = 0;
    absolute_time_t frame_start = get_absolute_time();

    while (1)
    {
        // Process frame loading continuously
        frame_loader_process();

        // Display frame if ready
        if (frame_loader_has_frame(current_frame) && display_manager_is_ready())
        {
            const uint16_t *frame_data = frame_loader_get_frame(current_frame);
            if (frame_data)
            {
                display_manager_show_frame(frame_data);

                // Setup next frames to load
                int next_frame = (current_frame + 1) % TOTAL_FRAMES;
                int frame_after_next = (current_frame + 2) % TOTAL_FRAMES; // Back to 2 frames ahead
                frame_loader_mark_frame_consumed(current_frame, frame_after_next);

                current_frame = next_frame;

                // Simple frame timing - wait for target frame time
                absolute_time_t now = get_absolute_time();
                int64_t frame_time = absolute_time_diff_us(frame_start, now);
                int64_t sleep_time = TARGET_FRAME_US - frame_time;

                if (sleep_time > 1000)
                {
                    sleep_us(sleep_time - 500); // Small buffer for precision
                }

                frame_start = get_absolute_time();
            }
        }
        else
        {
            // No frame ready, brief yield to prevent busy waiting
            sleep_us(100);
        }
    }

    return 0;
}