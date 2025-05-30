#include <stdio.h>
#include <string.h> // For strcpy, etc.
#include "pico/stdlib.h"
#include "pico/time.h"       // Added for profiling
#include "hardware/clocks.h" // <<< ADD THIS INCLUDE
#include "ff.h"              // FatFS library
#include "sd_card.h"         // SD card driver functions
#include "bsp_co5300.h"      // CO5300 display driver
#include "dma_config.h"      // Include the new DMA config header
#include "sd_loader.h"       // Include the new SD loader module
// #include "palette_rgb565.h"  // No longer needed for direct RGB565 rendering

// Display dimensions
#define PHYSICAL_DISPLAY_WIDTH 466
#define PHYSICAL_DISPLAY_HEIGHT 466
#define FRAME_WIDTH 156
#define FRAME_HEIGHT 156
#define SCALED_WIDTH (FRAME_WIDTH * 2)   // 312
#define SCALED_HEIGHT (FRAME_HEIGHT * 2) // 312
#define DISPLAY_WIDTH SCALED_WIDTH       // Use scaled dimensions
#define DISPLAY_HEIGHT SCALED_HEIGHT     // Use scaled dimensions

// RGB332 Color definitions
#define RED_COLOR_RGB332 0xE0   // 11100000
#define GREEN_COLOR_RGB332 0x1C // 00011100
#define BLUE_COLOR_RGB332 0x03  // 00000011
#define BLACK_COLOR_RGB332 0x00 // 00000000

#define MAX_FILENAME_LEN 64
#define MAX_FRAMES 4000 // Max number of frames we can list in the manifest - Updated from 100 to support 3.4k+ frames

// Global 256-color palette (RGB565 format)
// TODO: You MUST populate this palette with the colors used by your 8-bit images!
// Example: palette[indexed_color] = rgb565_color;
// static uint16_t global_palette[256]; // REMOVED - Now using generated_global_palette from palette_rgb565.h

// Precomputed scaling maps
static uint16_t source_y_map[DISPLAY_HEIGHT];
static uint16_t source_x_map[DISPLAY_WIDTH];

volatile bool dma_transfer_complete = true; // Flag for DMA completion (for display flushing)

// Callback function for DMA
void dma_done_callback(void)
{
    dma_transfer_complete = true; // Signal DMA completion
}

// Function to initialize precomputed scaling maps
void init_scaling_maps(void)
{
    printf("Initializing 2x scaling maps...\n");
    // Not needed for integer scaling, but keep for future flexibility
    for (int i = 0; i < DISPLAY_HEIGHT; i++)
    {
        source_y_map[i] = i >> 1; // Divide by 2
    }
    for (int i = 0; i < DISPLAY_WIDTH; i++)
    {
        source_x_map[i] = i >> 1; // Divide by 2
    }
    printf("2x scaling maps initialized.\n");
}

// Function to clear the entire physical screen to black
void clear_entire_screen_to_black(void)
{
    printf("Clearing physical screen to black...\n");
    bsp_co5300_set_window(0, 0, PHYSICAL_DISPLAY_WIDTH - 1, PHYSICAL_DISPLAY_HEIGHT - 1);
    bsp_co5300_prepare_for_frame_pixels();

    uint8_t black_line[PHYSICAL_DISPLAY_WIDTH]; // Changed to uint8_t
    for (int i = 0; i < PHYSICAL_DISPLAY_WIDTH; i++)
    {
        black_line[i] = BLACK_COLOR_RGB332; // Black in RGB332
    }

    for (int y = 0; y < PHYSICAL_DISPLAY_HEIGHT; y++)
    {
        while (!dma_transfer_complete)
        {
            sleep_us(0);
        }
        dma_transfer_complete = false;
        bsp_co5300_flush(black_line, PHYSICAL_DISPLAY_WIDTH); // Length is number of pixels (bytes for 8-bit)
    }
    while (!dma_transfer_complete)
    { // Wait for the last flush
        sleep_us(0);
    }
    bsp_co5300_finish_frame_pixels();
    printf("Physical screen cleared.\n");
}

// Function to display a test pattern
void display_test_pattern(void)
{
    printf("Displaying test pattern\n");
    bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    uint8_t test_pattern_line[DISPLAY_WIDTH]; // Changed to uint8_t
    for (int i = 0; i < DISPLAY_WIDTH; i++)
    {
        test_pattern_line[i] = (i % 2 == 0) ? RED_COLOR_RGB332 : BLUE_COLOR_RGB332;
    }
    bsp_co5300_prepare_for_frame_pixels(); // Prepare once before the loop
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        while (!dma_transfer_complete)
        {
            sleep_us(0);
        }
        dma_transfer_complete = false;
        bsp_co5300_flush(test_pattern_line, DISPLAY_WIDTH);
    }
    while (!dma_transfer_complete)
    {
        sleep_us(0);
    } // Wait for last flush
    bsp_co5300_finish_frame_pixels();
    printf("Test pattern displayed.\n");
}

int main()
{
    sleep_ms(100); // Reduced from 2000ms - just enough for serial init
    stdio_init_all();
    // Set system clock to 150 MHz for RP2350
    // Must be called before PLLs are initialized by other functions (e.g. stdio_init_all might init USB PLL)
    // However, for safety and common practice, it's often set very early.
    // Check RP2350 datasheet & SDK docs for precise timing if issues arise.
    set_sys_clock_khz(150000, true); // <<< ADD THIS LINE
    sleep_ms(100);                   // Reduced from 2000ms - just enough for clock stability
    printf("MAIN: System Initialized. Clock: %lu Hz\n", clock_get_hz(clk_sys));

    // Initialize precomputed scaling maps
    init_scaling_maps();

    // Initialize Display (minimal parameters)
    printf("MAIN: Initializing display...\n");
    bsp_co5300_info_t display_info = {
        .width = PHYSICAL_DISPLAY_WIDTH,   // Use physical dimensions for init
        .height = PHYSICAL_DISPLAY_HEIGHT, // Use physical dimensions for init
        .x_offset = 6,                     // Adjusted based on working LVGL example
        .y_offset = 0,
        .brightness = 95,
        .enabled_dma = true,
        .dma_flush_done_callback = dma_done_callback};
    bsp_co5300_init(&display_info);
    printf("MAIN: Display initialized.\n");

    // Clear the entire screen to black first
    // clear_entire_screen_to_black();  // COMMENTED OUT - unnecessary overhead for centered playback

    // Initialize SD DMA (must be done after display potentially claims DMA_IRQ_0)
    printf("MAIN: Initializing general purpose DMA (formerly SD DMA)...\n");
    init_sd_dma(); // This is now a general purpose DMA channel claimer
    printf("MAIN: General purpose DMA initialized.\n");

    // --- SD CARD CODE ---
    printf("MAIN: Initializing SD card...\n");
    if (!sd_init_driver())
    {
        printf("ERROR: SD card initialization failed. Halting.\n");
        while (true)
        {
            tight_loop_contents();
        }
    }
    printf("MAIN: SD card mounted.\n");
    FATFS fs;
    FRESULT fr;
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK)
    {
        printf("ERROR: Failed to mount SD card. FatFS error code: %d\n", fr);
        while (true)
        {
            tight_loop_contents();
        }
    }

    printf("MAIN: Checking for animation frames...\n");

    int num_frames = 0;
    FIL test_fil;
    char test_filename[MAX_FILENAME_LEN];

    // Try to open first frame to verify pattern start
    snprintf(test_filename, MAX_FILENAME_LEN, "frame-%05d.bin", 0);
    fr = f_open(&test_fil, test_filename, FA_READ);
    if (fr != FR_OK)
    {
        printf("ERROR: Failed to open first frame %s. FR_CODE: %d\n", test_filename, fr);
        while (true)
        {
            tight_loop_contents();
        }
    }
    f_close(&test_fil);

    // Try to open what we think is the last frame (based on manifest)
    snprintf(test_filename, MAX_FILENAME_LEN, "frame-%05d.bin", 3402);
    fr = f_open(&test_fil, test_filename, FA_READ);
    if (fr != FR_OK)
    {
        printf("Scanning for last frame...\n");

        // Scan backwards from 3402 to find the last valid frame
        for (int i = 3401; i >= 0; i--)
        {
            snprintf(test_filename, MAX_FILENAME_LEN, "frame-%05d.bin", i);
            fr = f_open(&test_fil, test_filename, FA_READ);
            if (fr == FR_OK)
            {
                f_close(&test_fil);
                num_frames = i + 1;
                break;
            }
        }
    }
    else
    {
        f_close(&test_fil);
        num_frames = 3403; // We found frame-03402.bin, so we have 3403 frames (0-3402)
    }

    printf("Found %d sequential frames.\n", num_frames);

    // Initialize the SD Loader module
    sd_loader_init(num_frames);

    if (num_frames == 0)
    {
        printf("No frames found. Displaying error pattern.\n");
        uint8_t error_colors[] = {RED_COLOR_RGB332, BLUE_COLOR_RGB332, GREEN_COLOR_RGB332};
        int error_color_index = 0;
        while (1)
        {
            bsp_co5300_set_window(0, 0, PHYSICAL_DISPLAY_WIDTH - 1, PHYSICAL_DISPLAY_HEIGHT - 1);
            uint8_t error_line_buffer[PHYSICAL_DISPLAY_WIDTH];
            for (int i = 0; i < PHYSICAL_DISPLAY_WIDTH; i++)
            {
                error_line_buffer[i] = error_colors[error_color_index];
            }
            bsp_co5300_prepare_for_frame_pixels();
            for (int y = 0; y < PHYSICAL_DISPLAY_HEIGHT; y++)
            {
                while (!dma_transfer_complete)
                {
                    sleep_us(0);
                }
                dma_transfer_complete = false;
                bsp_co5300_flush(error_line_buffer, PHYSICAL_DISPLAY_WIDTH);
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

// Buffer to hold two entire source frames in RAM for double buffering - MOVED TO SD_LOADER
// static uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH]; // THIS LINE SHOULD REMAIN COMMENTED OR BE DELETED
// int current_buffer = 0; // Will be replaced by display_buffer_idx_for_display

// DMA line buffers (for display)
// Define how many lines to buffer for each DMA transfer
#define LINES_PER_BUFFER FRAME_HEIGHT                              // Process entire frame at once - we have the RAM
    uint8_t frame_line_buffer_a[DISPLAY_WIDTH * LINES_PER_BUFFER]; // Changed to uint8_t
    uint8_t frame_line_buffer_b[DISPLAY_WIDTH * LINES_PER_BUFFER]; // Changed to uint8_t
    volatile uint8_t *cpu_buffer_ptr;                              // Changed to uint8_t*
    volatile uint8_t *dma_buffer_ptr;                              // Changed to uint8_t*

    // FIL frame_fil; // Moved to sd_loader.c
    // UINT bytes_read_for_full_frame; // Not needed with chunked loading this way
    // char current_frame_full_path[MAX_FILENAME_LEN + 8]; // Handled by sd_loader

    printf("Starting 8-bit RGB332 animation loop (%d frames, %dx%d -> %dx%d).\n", num_frames, FRAME_WIDTH, FRAME_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Pre-load first frame into buffer 0 - REMOVED (handled by sd_loader_process calls)
    // snprintf(current_frame_full_path, sizeof(current_frame_full_path), "output/%s", frame_filenames[0]);
    // fr = f_open(&frame_fil, current_frame_full_path, FA_READ);
    // if (fr == FR_OK)
    // {
    //     fr = f_read(&frame_fil, frame_buffers[0], FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint16_t), &bytes_read_for_full_frame);
    //     f_close(&frame_fil);
    //     if (fr == FR_OK) buffer_ready[0] = true; // Old buffer_ready
    // }

    int display_frame_idx = 0;  // The frame index we intend to display next
    int display_buffer_idx = 0; // The buffer (0 or 1) we expect to find display_frame_idx in

    // Calculate offsets for centering 312x312 content on 466x466 display
    const int x_offset_centered = (PHYSICAL_DISPLAY_WIDTH - SCALED_WIDTH) / 2;
    const int y_offset_centered = (PHYSICAL_DISPLAY_HEIGHT - SCALED_HEIGHT) / 2;

    printf("Entering main display & loader loop (2x scaling)\n");

    absolute_time_t loop_start_time, loop_end_time;
    absolute_time_t sd_load_start_time, sd_load_end_time;
    absolute_time_t display_prep_start_time, display_prep_end_time;
    absolute_time_t display_render_start_time, display_render_end_time;

    // Allocate line buffers for 2x scaling
    uint8_t scaled_line_buffer[SCALED_WIDTH];

    while (1)
    {
        loop_start_time = get_absolute_time();

        // Process SD card loading
        sd_load_start_time = get_absolute_time();
        sd_loader_process();
        sd_load_end_time = get_absolute_time();

        if (buffer_ready[display_buffer_idx] &&
            sd_loader_get_target_frame_for_buffer(display_buffer_idx) == display_frame_idx)
        {
            display_render_start_time = get_absolute_time();

            // Set up display window for scaled content
            bsp_co5300_set_window(x_offset_centered, y_offset_centered,
                                  x_offset_centered + SCALED_WIDTH - 1,
                                  y_offset_centered + SCALED_HEIGHT - 1);
            bsp_co5300_prepare_for_frame_pixels();

            // Render frame with 2x scaling
            uint8_t *src_buffer = (uint8_t *)frame_buffers[display_buffer_idx];

            for (int y = 0; y < FRAME_HEIGHT; y++)
            {
                // Scale each source line to 2x width
                for (int x = 0; x < FRAME_WIDTH; x++)
                {
                    uint8_t pixel = src_buffer[y * FRAME_WIDTH + x];
                    // Each source pixel becomes 2x2 pixels
                    scaled_line_buffer[x * 2] = pixel;
                    scaled_line_buffer[x * 2 + 1] = pixel;
                }

                // Output the scaled line twice for 2x vertical scaling
                while (!dma_transfer_complete)
                    tight_loop_contents();
                dma_transfer_complete = false;
                bsp_co5300_flush(scaled_line_buffer, SCALED_WIDTH);

                while (!dma_transfer_complete)
                    tight_loop_contents();
                dma_transfer_complete = false;
                bsp_co5300_flush(scaled_line_buffer, SCALED_WIDTH);
            }

            // Wait for the last DMA transfer
            while (!dma_transfer_complete)
                tight_loop_contents();
            bsp_co5300_finish_frame_pixels();

            display_render_end_time = get_absolute_time();

            // Frame display complete, advance to next
            int next_frame_to_target = display_frame_idx + 2;

            // Handle frame wrapping
            if (next_frame_to_target >= num_frames)
            {
                printf("Main loop wrapping from frame %d back to %d (total frames: %d)\n",
                       next_frame_to_target, next_frame_to_target % num_frames, num_frames);
            }

            // Mark current buffer as consumed and set its next target
            sd_loader_mark_buffer_consumed(display_buffer_idx, next_frame_to_target);

            // Advance to next frame, ensuring we wrap around properly
            display_frame_idx = (display_frame_idx + 1) % num_frames;

            // Switch to other buffer
            display_buffer_idx = 1 - display_buffer_idx;

            // Print profiling info at the end of each loop
            if (display_frame_idx == 0) // This signifies a completed playthrough
            {
                loop_end_time = get_absolute_time();
                printf("LOOP_COMPLETE: SD Load: %lld us, Display Render: %lld us, Full Loop: %lld us\n",
                       absolute_time_diff_us(sd_load_start_time, sd_load_end_time),
                       absolute_time_diff_us(display_render_start_time, display_render_end_time),
                       absolute_time_diff_us(loop_start_time, loop_end_time));
            }
        }
        else
        {
            // Expected buffer not ready or contains wrong frame
            tight_loop_contents();

            // Print debug info
            loop_end_time = get_absolute_time();
            printf("WAITING: buf_idx=%d, frame_idx=%d, B0_ready=%d,T0=%d, B1_ready=%d,T1=%d\n",
                   display_buffer_idx, display_frame_idx,
                   buffer_ready[0], sd_loader_get_target_frame_for_buffer(0),
                   buffer_ready[1], sd_loader_get_target_frame_for_buffer(1));
        }
    } // end while(1)

    return 0;
}