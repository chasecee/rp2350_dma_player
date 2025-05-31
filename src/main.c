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
#include "debug.h"           // Add debug macro header
// #include "palette_rgb565.h"  // No longer needed for direct RGB565 rendering

// Display dimensions
#define PHYSICAL_DISPLAY_WIDTH 466
#define PHYSICAL_DISPLAY_HEIGHT 466
#define FRAME_WIDTH 156
#define FRAME_HEIGHT 156
#define DISPLAY_WIDTH PHYSICAL_DISPLAY_WIDTH
#define DISPLAY_HEIGHT PHYSICAL_DISPLAY_HEIGHT

// Scaling factors (fixed point with 8 bits of fraction)
#define SCALE_FACTOR_X ((DISPLAY_WIDTH << 8) / FRAME_WIDTH)
#define SCALE_FACTOR_Y ((DISPLAY_HEIGHT << 8) / FRAME_HEIGHT)

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
    DBG_PRINTF("Initializing scaling maps for %dx%d -> %dx%d...\n",
               FRAME_WIDTH, FRAME_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Precompute source Y coordinates for each destination Y
    for (int i = 0; i < DISPLAY_HEIGHT; i++)
    {
        source_y_map[i] = (i * FRAME_HEIGHT) / DISPLAY_HEIGHT;
    }

    // Precompute source X coordinates for each destination X
    for (int i = 0; i < DISPLAY_WIDTH; i++)
    {
        source_x_map[i] = (i * FRAME_WIDTH) / DISPLAY_WIDTH;
    }

    DBG_PRINTF("Scaling maps initialized.\n");
}

// Function to clear the entire physical screen to black
void clear_entire_screen_to_black(void)
{
    DBG_PRINTF("Clearing physical screen to black...\n");
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
    DBG_PRINTF("Physical screen cleared.\n");
}

// Function to display a test pattern
void display_test_pattern(void)
{
    DBG_PRINTF("Displaying test pattern\n");
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
    DBG_PRINTF("Test pattern displayed.\n");
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
    DBG_PRINTF("MAIN: System Initialized. Clock: %lu Hz\n", clock_get_hz(clk_sys));

    // Initialize precomputed scaling maps
    init_scaling_maps();

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
    bsp_co5300_init(&display_info);
    DBG_PRINTF("MAIN: Display initialized.\n");

    // Clear the entire screen to black first
    // clear_entire_screen_to_black();  // COMMENTED OUT - unnecessary overhead for centered playback

    // Initialize SD DMA (must be done after display potentially claims DMA_IRQ_0)
    DBG_PRINTF("MAIN: Initializing general purpose DMA (formerly SD DMA)...\n");
    init_sd_dma(); // This is now a general purpose DMA channel claimer
    DBG_PRINTF("MAIN: General purpose DMA initialized.\n");

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
    DBG_PRINTF("MAIN: SD card mounted.\n");
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

    DBG_PRINTF("MAIN: Checking for animation frames...\n");

    int num_frames = 0;
    FIL frames_bin_fil;
    fr = f_open(&frames_bin_fil, "frames.bin", FA_READ);
    if (fr != FR_OK)
    {
        printf("ERROR: Failed to open frames.bin. FR_CODE: %d\n", fr);
        while (true)
        {
            tight_loop_contents();
        }
    }
    // Get file size and compute number of frames
    uint32_t frames_bin_size = f_size(&frames_bin_fil);
    num_frames = frames_bin_size / (FRAME_WIDTH * FRAME_HEIGHT);
    f_close(&frames_bin_fil);

    DBG_PRINTF("Found %d frames in frames.bin.\n", num_frames);

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

    DBG_PRINTF("Starting 8-bit RGB332 animation loop (%d frames, %dx%d -> %dx%d).\n",
               num_frames, FRAME_WIDTH, FRAME_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Pre-load first frame into buffer 0 - REMOVED (handled by sd_loader_process calls)
    // snprintf(current_frame_full_path, sizeof(current_frame_full_path), "output/%s", frame_filenames[0]);
    // fr = f_open(&frame_fil, current_frame_full_path, FA_READ);
    // if (fr == FR_OK)
    // {
    //     fr = f_read(&frame_fil, frame_buffers[0], FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint16_t), &bytes_read_for_full_frame);
    //     f_close(&frame_fil);
    //     if (fr == FR_OK) buffer_ready[0] = true; // Old buffer_ready
    // }

    int display_frame_idx = 0;
    int display_buffer_idx = 0;

    // 3x integer scaling: for each display pixel, use the corresponding input pixel at (y/3, x/3)
    const int x_offset = 0;
    const int y_offset = 0;
    DBG_PRINTF("Entering main display & loader loop (3x integer scaling from 156x156 to 466x466)\n");

    absolute_time_t frame_start_time = get_absolute_time();
    const uint32_t target_frame_us = 33333; // Target ~30fps (33.333ms)

    // Allocate a line buffer for the display
    uint8_t *scaled_line_buffer = malloc(DISPLAY_WIDTH);
    if (!scaled_line_buffer)
    {
        printf("Failed to allocate scaled line buffer!\n");
        while (1)
            tight_loop_contents();
    }

    while (1)
    {
        // Process SD card loading first
        sd_loader_process();

        // Check if either buffer has the frame we want to display
        int buffer_with_frame = -1;
        if (buffer_ready[0] && sd_loader_get_target_frame_for_buffer(0) == display_frame_idx)
        {
            buffer_with_frame = 0;
        }
        else if (buffer_ready[1] && sd_loader_get_target_frame_for_buffer(1) == display_frame_idx)
        {
            buffer_with_frame = 1;
        }

        if (buffer_with_frame >= 0)
        {
            // Set up display window for full content
            bsp_co5300_set_window(x_offset, y_offset,
                                  x_offset + DISPLAY_WIDTH - 1,
                                  y_offset + DISPLAY_HEIGHT - 1);
            bsp_co5300_prepare_for_frame_pixels();

            // Get pointer to source frame
            uint8_t *src_buffer = frame_buffers[buffer_with_frame];

            // 3x integer scaling: for each display line, fill from the corresponding input line
            for (int y = 0; y < DISPLAY_HEIGHT; y++)
            {
                int src_y = y / 3;
                uint8_t *src_line = &src_buffer[src_y * FRAME_WIDTH];
                for (int x = 0; x < DISPLAY_WIDTH; x++)
                {
                    int src_x = x / 3;
                    scaled_line_buffer[x] = src_line[src_x];
                }
                while (!dma_transfer_complete)
                    tight_loop_contents();
                dma_transfer_complete = false;
                bsp_co5300_flush(scaled_line_buffer, DISPLAY_WIDTH);
            }

            // Wait for the last DMA transfer
            while (!dma_transfer_complete)
                tight_loop_contents();
            bsp_co5300_finish_frame_pixels();

            // Frame display complete, advance to next
            int next_frame_to_target = display_frame_idx + 2;

            // Mark current buffer as consumed and set its next target
            sd_loader_mark_buffer_consumed(buffer_with_frame, next_frame_to_target % num_frames);

            // Advance to next frame
            display_frame_idx = (display_frame_idx + 1) % num_frames;

            // Frame timing control
            absolute_time_t now = get_absolute_time();
            int64_t frame_time = absolute_time_diff_us(frame_start_time, now);

            if (frame_time < target_frame_us)
            {
                // We rendered faster than target frame time, sleep for remainder
                sleep_us(target_frame_us - frame_time);
            }

            frame_start_time = get_absolute_time();
        }
        else
        {
            // No buffer has our frame yet - just keep processing
            tight_loop_contents();
        }
    }

    return 0;
}