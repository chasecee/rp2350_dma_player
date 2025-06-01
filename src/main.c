#include <stdio.h>
#include <string.h> // For strcpy, etc.
#include "pico/stdlib.h"
#include "pico/time.h" // Added for profiling
#include "hardware/clocks.h"
#include "diskio.h"        // Direct disk access (no FatFS!)
#include "sd_card.h"       // SD card driver functions
#include "bsp_co5300.h"    // CO5300 display driver
#include "raw_sd_loader.h" // Raw SD loader module
#include "debug.h"         // Debug macro header

// Display dimensions
#define PHYSICAL_DISPLAY_WIDTH 466
#define PHYSICAL_DISPLAY_HEIGHT 466
#define FRAME_WIDTH 233
#define FRAME_HEIGHT 233

// Set to 1 for native 233x233 output, 0 for 466x466 scaled output
#define NATIVE_OUTPUT 1

#if NATIVE_OUTPUT
#define DISPLAY_WIDTH 233
#define DISPLAY_HEIGHT 233
#else
#define DISPLAY_WIDTH PHYSICAL_DISPLAY_WIDTH
#define DISPLAY_HEIGHT PHYSICAL_DISPLAY_HEIGHT
#endif

// RGB565 Color definitions
#define RED_COLOR_RGB565 0xF800   // 1111100000000000
#define GREEN_COLOR_RGB565 0x07E0 // 0000011111100000
#define BLUE_COLOR_RGB565 0x001F  // 0000000000011111
#define BLACK_COLOR_RGB565 0x0000 // 0000000000000000

volatile bool dma_transfer_complete = true; // Flag for DMA completion (for display flushing)

// Callback function for DMA
void dma_done_callback(void)
{
    dma_transfer_complete = true; // Signal DMA completion
}

// Function to clear the entire physical screen to black
void clear_entire_screen_to_black(void)
{
    DBG_PRINTF("Clearing physical screen to black (16-bit)...\n");
    bsp_co5300_set_window(0, 0, PHYSICAL_DISPLAY_WIDTH - 1, PHYSICAL_DISPLAY_HEIGHT - 1);
    bsp_co5300_prepare_for_frame_pixels();

    static uint16_t black_line_16bit[PHYSICAL_DISPLAY_WIDTH]; // Use static to avoid large stack allocation
    for (int i = 0; i < PHYSICAL_DISPLAY_WIDTH; i++)
    {
        black_line_16bit[i] = BLACK_COLOR_RGB565; // Black in RGB565
    }

    for (int y = 0; y < PHYSICAL_DISPLAY_HEIGHT; y++)
    {
        while (!dma_transfer_complete)
        {
            sleep_us(0);
        }
        dma_transfer_complete = false;
        bsp_co5300_flush((uint8_t *)black_line_16bit, PHYSICAL_DISPLAY_WIDTH * 2); // Length is number of bytes for 16-bit
    }
    while (!dma_transfer_complete)
    { // Wait for the last flush
        sleep_us(0);
    }
    bsp_co5300_finish_frame_pixels();
    DBG_PRINTF("Physical screen cleared.\n");
}

// Function to display a test pattern
void display_test_pattern(void)
{
    DBG_PRINTF("Displaying test pattern (16-bit)\n");
    bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    static uint16_t test_pattern_line_16bit[DISPLAY_WIDTH]; // Use static for large array
    for (int i = 0; i < DISPLAY_WIDTH; i++)
    {
        test_pattern_line_16bit[i] = (i % 2 == 0) ? RED_COLOR_RGB565 : BLUE_COLOR_RGB565;
    }
    bsp_co5300_prepare_for_frame_pixels(); // Prepare once before the loop
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        while (!dma_transfer_complete)
        {
            sleep_us(0);
        }
        dma_transfer_complete = false;
        bsp_co5300_flush((uint8_t *)test_pattern_line_16bit, DISPLAY_WIDTH * 2); // Send bytes
    }
    while (!dma_transfer_complete)
    {
        sleep_us(0);
    } // Wait for last flush
    bsp_co5300_finish_frame_pixels();
    DBG_PRINTF("Test pattern displayed.\n");
}

int main()
{
    printf("Hello, Pico!\n"); // Early debug print
    sleep_ms(100);            // Reduced from 2000ms - just enough for serial init
    stdio_init_all();
    // Set system clock to 150 MHz for RP2350
    set_sys_clock_khz(150000, true);
    sleep_ms(100);
    DBG_PRINTF("MAIN: System Initialized. Clock: %lu Hz\n", clock_get_hz(clk_sys));

    // Initialize Display (minimal parameters)
    DBG_PRINTF("MAIN: Initializing display...\n");
    bsp_co5300_info_t display_info = {
        .width = PHYSICAL_DISPLAY_WIDTH,
        .height = PHYSICAL_DISPLAY_HEIGHT,
        .x_offset = 6,
        .y_offset = 0,
        .brightness = 95,
        .enabled_dma = true,
        .dma_flush_done_callback = dma_done_callback};
    if (!display_info.dma_flush_done_callback)
    {
        printf("ERROR: dma_flush_done_callback is NULL!\n");
        while (1)
            ;
    }
    bsp_co5300_init(&display_info);
    DBG_PRINTF("MAIN: Display initialized.\n");

    // Clear the entire screen to black first
    clear_entire_screen_to_black();

    // --- SD CARD CODE ---
    DBG_PRINTF("MAIN: Initializing SD card...\n");
    if (!sd_init_driver())
    {
        printf("ERROR: SD card initialization failed. Halting.\n");
        while (true)
        {
            tight_loop_contents();
        }
    }

    // Initialize disk layer directly (no filesystem needed for raw sector access)
    DSTATUS disk_status = disk_initialize(0); // Physical drive 0
    if (disk_status != 0)
    {
        printf("ERROR: Failed to initialize disk for raw access. Status: %d\n", disk_status);
        while (true)
        {
            tight_loop_contents();
        }
    }

    DBG_PRINTF("MAIN: SD card initialized for raw sector access.\n");

    DBG_PRINTF("MAIN: Checking for animation frames...\n");

    int num_frames = 0;

    // For raw SD mode, we calculate frames from known data
    // Each frame is FRAME_WIDTH * FRAME_HEIGHT * 2 bytes (RGB565)
    const uint32_t frame_size_bytes = FRAME_WIDTH * FRAME_HEIGHT * 2;

    // Using the actual number we know was written to frames.bin
    // TODO: Could read from metadata file or calculate from sectors used
    num_frames = 3403; // This matches your actual animation

    DBG_PRINTF("RAW_SD_LOADER: Using %d frames (%u bytes per frame)\n",
               num_frames, frame_size_bytes);
    DBG_PRINTF("RAW_SD_LOADER: Total animation data: %u bytes\n",
               num_frames * frame_size_bytes);

    // Initialize the RAW SD Loader module
    raw_sd_loader_init(num_frames);

    if (num_frames == 0)
    {
        printf("No frames found. Displaying error pattern.\n");
        uint16_t error_colors[] = {RED_COLOR_RGB565, BLUE_COLOR_RGB565, GREEN_COLOR_RGB565};
        int error_color_index = 0;
        while (1)
        {
            bsp_co5300_set_window(0, 0, PHYSICAL_DISPLAY_WIDTH - 1, PHYSICAL_DISPLAY_HEIGHT - 1);
            uint16_t error_line_buffer_16bit[PHYSICAL_DISPLAY_WIDTH];
            for (int i = 0; i < PHYSICAL_DISPLAY_WIDTH; i++)
            {
                error_line_buffer_16bit[i] = error_colors[error_color_index];
            }
            bsp_co5300_prepare_for_frame_pixels();
            for (int y = 0; y < PHYSICAL_DISPLAY_HEIGHT; y++)
            {
                while (!dma_transfer_complete)
                {
                    sleep_us(0);
                }
                dma_transfer_complete = false;
                bsp_co5300_flush((uint8_t *)error_line_buffer_16bit, PHYSICAL_DISPLAY_WIDTH * 2); // Send bytes
            }
            while (!dma_transfer_complete)
            {
                sleep_us(0);
            }
            bsp_co5300_finish_frame_pixels();
            error_color_index = (error_color_index + 1) % 3;
            sleep_ms(500);
        }
    }

    DBG_PRINTF("Starting 16-bit RGB565 animation loop (%d frames, %dx%d native display).\n",
               num_frames, FRAME_WIDTH, FRAME_HEIGHT);

    // Debug: Print frame size information
    uint32_t expected_frame_size = FRAME_WIDTH * FRAME_HEIGHT * 2; // 2 bytes per pixel
    DBG_PRINTF("Expected frame size: %lu bytes (%dx%d*2)\n", expected_frame_size, FRAME_WIDTH, FRAME_HEIGHT);
    DBG_PRINTF("Calculated centering offset: x=%d, y=%d\n",
               (PHYSICAL_DISPLAY_WIDTH - DISPLAY_WIDTH) / 2,
               (PHYSICAL_DISPLAY_HEIGHT - DISPLAY_HEIGHT) / 2);

    int display_frame_idx = 0;
    int display_buffer_idx = 0;

    // Calculate display offsets - used for native mode centering
    const int x_offset = (PHYSICAL_DISPLAY_WIDTH - DISPLAY_WIDTH) / 2;   // Center horizontally
    const int y_offset = (PHYSICAL_DISPLAY_HEIGHT - DISPLAY_HEIGHT) / 2; // Center vertically
#if NATIVE_OUTPUT
    DBG_PRINTF("Entering main display & loader loop (native 233x233 centered on 466x466)\n");
#else
    DBG_PRINTF("Entering main display & loader loop (scaled 233x233 -> 466x466 fullscreen)\n");
#endif

    absolute_time_t frame_start_time = get_absolute_time();
    const uint32_t target_frame_us = 33333; // Target ~30fps (33.333ms)

    // Profiling variables
    uint32_t sd_load_time_us = 0;
    uint32_t display_time_us = 0;
    uint32_t frame_counter = 0;
    absolute_time_t profile_start;

    // For native display, use moderate multi-line buffer - large batches cause display duplication
#if NATIVE_OUTPUT
#define LINES_PER_DMA 233 // Native mode: process all lines at once
    uint16_t *multi_line_buffer = malloc(DISPLAY_WIDTH * LINES_PER_DMA * sizeof(uint16_t));
    if (!multi_line_buffer)
    {
        printf("Failed to allocate multi-line buffer!\n");
        while (1)
            tight_loop_contents();
    }
#else
// Scaled mode: use smaller line-based buffer to conserve memory
#define SCALED_LINES_PER_BATCH 2 // Process just 2 lines at a time to minimize memory
    uint16_t *display_line_buffer = malloc(DISPLAY_WIDTH * SCALED_LINES_PER_BATCH * sizeof(uint16_t));
    if (!display_line_buffer)
    {
        printf("Failed to allocate display line buffer!\n");
        while (1)
            tight_loop_contents();
    }
#endif

    while (1)
    {
        // Process SD card loading first
        profile_start = get_absolute_time();
        raw_sd_loader_process();
        sd_load_time_us = absolute_time_diff_us(profile_start, get_absolute_time());

        // Check which buffer has the frame we want to display
        int buffer_to_display = -1;
        for (int i = 0; i < 2; i++)
        {
            if (buffer_ready[i] && raw_sd_loader_get_target_frame_for_buffer(i) == display_frame_idx)
            {
                buffer_to_display = i;
                break;
            }
        }

        if (buffer_to_display >= 0)
        {
            DBG_PRINTF("MAIN: Displaying frame %d from buffer %d\n", display_frame_idx, buffer_to_display);

            profile_start = get_absolute_time();

#if NATIVE_OUTPUT
            // Native 233x233 mode - center on display
            bsp_co5300_set_window(x_offset, y_offset,
                                  x_offset + DISPLAY_WIDTH - 1,
                                  y_offset + DISPLAY_HEIGHT - 1);
            bsp_co5300_prepare_for_frame_pixels();

            // Direct copy - no scaling needed
            int lines_processed = 0;

            for (int batch_start = 0; batch_start < DISPLAY_HEIGHT; batch_start += LINES_PER_DMA)
            {
                int lines_in_batch = LINES_PER_DMA;
                if (batch_start + lines_in_batch > DISPLAY_HEIGHT)
                    lines_in_batch = DISPLAY_HEIGHT - batch_start;

                // Clear the multi-line buffer to prevent stale data artifacts
                memset(multi_line_buffer, 0, DISPLAY_WIDTH * LINES_PER_DMA * sizeof(uint16_t));

                // Copy lines from frame buffer to multi-line buffer
                for (int line = 0; line < lines_in_batch; line++)
                {
                    int src_y = batch_start + line;

                    // Bounds checking - ensure we don't read past frame boundaries
                    if (src_y >= FRAME_HEIGHT)
                    {
                        break;
                    }

                    uint16_t *src_line = &frame_buffers[buffer_to_display][src_y * FRAME_WIDTH];
                    uint16_t *dst_line = &multi_line_buffer[line * DISPLAY_WIDTH];

                    // Copy directly - convert.py now outputs in correct byte order
                    for (int x = 0; x < DISPLAY_WIDTH && x < FRAME_WIDTH; x++)
                    {
                        dst_line[x] = src_line[x];
                    }
                }

                // Send this batch via DMA - only send the actual lines needed
                while (!dma_transfer_complete)
                    tight_loop_contents();
                dma_transfer_complete = false;
                bsp_co5300_flush((uint8_t *)multi_line_buffer,
                                 DISPLAY_WIDTH * lines_in_batch * sizeof(uint16_t));
                lines_processed += lines_in_batch;
            }
#else
            // Scaled 466x466 mode - 2x2 pixel scaling with memory-efficient batching

            // Set up display window once for the entire frame
            bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
            bsp_co5300_prepare_for_frame_pixels();

            // Process frame in batches to conserve memory
            for (int batch_start = 0; batch_start < DISPLAY_HEIGHT; batch_start += SCALED_LINES_PER_BATCH)
            {
                int lines_in_batch = SCALED_LINES_PER_BATCH;
                if (batch_start + lines_in_batch > DISPLAY_HEIGHT)
                    lines_in_batch = DISPLAY_HEIGHT - batch_start;

                // Clear the batch buffer
                memset(display_line_buffer, 0, DISPLAY_WIDTH * lines_in_batch * sizeof(uint16_t));

                // Scale source lines into this batch
                for (int batch_line = 0; batch_line < lines_in_batch; batch_line++)
                {
                    int dst_y = batch_start + batch_line;
                    int src_y = dst_y / 2; // Scale factor of 2

                    uint16_t *dst_line = &display_line_buffer[batch_line * DISPLAY_WIDTH];

                    // Only process if we have valid source data
                    if (src_y < FRAME_HEIGHT)
                    {
                        uint16_t *src_line = &frame_buffers[buffer_to_display][src_y * FRAME_WIDTH];

                        // Scale horizontally: each source pixel becomes 2 destination pixels
                        for (int src_x = 0; src_x < FRAME_WIDTH; src_x++)
                        {
                            uint16_t pixel = src_line[src_x];
                            int dst_x = src_x * 2;

                            // Write the 2 horizontal pixels
                            if (dst_x < DISPLAY_WIDTH)
                                dst_line[dst_x] = pixel;
                            if (dst_x + 1 < DISPLAY_WIDTH)
                                dst_line[dst_x + 1] = pixel;
                        }
                    }
                    // If src_y >= FRAME_HEIGHT, line remains black (from memset)
                }

                // Send this batch - continuous stream, no window reset
                while (!dma_transfer_complete)
                    tight_loop_contents();
                dma_transfer_complete = false;
                bsp_co5300_flush((uint8_t *)display_line_buffer,
                                 DISPLAY_WIDTH * lines_in_batch * sizeof(uint16_t));
            }
#endif

            // Wait for the last DMA transfer to complete
            while (!dma_transfer_complete)
                tight_loop_contents();
            bsp_co5300_finish_frame_pixels();

            display_time_us = absolute_time_diff_us(profile_start, get_absolute_time());

            // Frame display complete, advance to next
            // Calculate which frame we'll need after this one
            int next_frame = (display_frame_idx + 1) % num_frames;
            int frame_after_next = (display_frame_idx + 2) % num_frames;

            // Mark this buffer as consumed and assign it to load frame_after_next
            raw_sd_loader_mark_buffer_consumed(buffer_to_display, frame_after_next);

            // Advance to next display frame
            display_frame_idx = next_frame;
            DBG_PRINTF("MAIN: Advanced to frame %d (next target: %d)\n", display_frame_idx, frame_after_next);

            // Frame timing control
            absolute_time_t now = get_absolute_time();
            int64_t frame_time = absolute_time_diff_us(frame_start_time, now);

            // Print profiling info every 30 frames (1 second at 30fps)
            if (++frame_counter % 30 == 0)
            {
                DBG_PRINTF("PROFILE: Frame %d - SD load: %lu us, Display: %lu us, Total: %lld us (Target: %lu us)\n",
                           frame_counter, sd_load_time_us, display_time_us, frame_time, target_frame_us);
            }

            // Improved frame timing - only sleep if we're significantly ahead of schedule
            if (frame_time < (target_frame_us - 1000)) // Leave 1ms buffer for timing precision
            {
                // We rendered faster than target frame time, sleep for remainder
                uint32_t sleep_time = target_frame_us - frame_time - 500; // Leave 500us buffer
                if (sleep_time > 0 && sleep_time < target_frame_us)
                {
                    sleep_us(sleep_time);
                }
            }

            frame_start_time = get_absolute_time(); // Start timing for next frame
        }
        else
        {
            // No buffer has our frame yet - just keep processing
            tight_loop_contents();
        }
    }

    return 0;
}