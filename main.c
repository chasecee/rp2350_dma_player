#include <stdio.h>
#include <string.h> // For strcpy, etc.
#include <math.h>   // For M_PI and cosf()
#include "pico/stdlib.h"
#include "ff.h"         // FatFS library
#include "sd_card.h"    // SD card driver functions
#include "bsp_co5300.h" // CO5300 display driver

// Display dimensions (assuming 466x466 based on README)
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define FRAME_WIDTH 140         // Updated for the new frame size
#define FRAME_HEIGHT 140        // Updated for the new frame size
#define SCALED_FRAME_WIDTH 140  // New: Apparent width of each tile
#define SCALED_FRAME_HEIGHT 140 // New: Apparent height of each tile

// Updated color definitions for 8-bit RGB332
#define RED_COLOR 0xE0   // Binary 11100000 (R:111, G:000, B:00)
#define GREEN_COLOR 0x1C // Binary 00011100 (R:000, G:111, B:00)
#define BLUE_COLOR 0x03  // Binary 00000011 (R:000, G:000, B:11)
#define BLACK_COLOR 0x00 // Binary 00000000
#define WHITE_COLOR 0xFF // Binary 11111111

#define MAX_FILENAME_LEN 64
#define TOTAL_ANIMATION_FRAMES 100 // User-specified total number of frames
#define FRAMES_TO_BUFFER 10        // Number of frames to keep in RAM

// Glitch effect parameters // REMOVED
// #define MAX_TARGET_GLITCH_PROBABILITY 0.005f // REMOVED
// #define GLITCH_PROBABILITY_PERIOD_SECONDS 500.0f // REMOVED
// #define MAX_GLITCH_LENGTH (FRAME_WIDTH / 3) // REMOVED
// #define MAX_GLITCH_OFFSET 4 // REMOVED
// #define MAX_GLITCH_BLOCK_HEIGHT_LINES 8 // REMOVED

// Static state for an active glitch block // REMOVED
// static int s_glitch_lines_remaining = 0; // REMOVED
// static int s_glitch_start_cpu_block; // REMOVED
// static int s_glitch_len_block; // REMOVED

volatile bool dma_transfer_complete = true; // Flag for DMA completion

// Callback function for DMA, still needed by bsp_co5300_init
void dma_done_callback(void)
{
    dma_transfer_complete = true; // Signal DMA completion
}

// Helper function to apply a glitch if one is active or start a new one // REMOVED
// static void apply_glitch_if_active(volatile uint8_t *cpu_buf, volatile uint8_t *dma_buf, float current_glitch_probability)
// { // REMOVED ENTIRE FUNCTION
// ... (entire function content removed) ...
// }

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("MINIMAL HELLO WORLD! Can you see me? (Attempting minimal display init)\n");

    // Initialize Display (minimal parameters)
    printf("Initializing display (minimal parameters)...\n");
    bsp_co5300_info_t display_info = {
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
        .x_offset = 6, // Adjusted based on working LVGL example
        .y_offset = 0,
        .brightness = 80,
        .enabled_dma = true, // DMA RE-ENABLED
        .dma_flush_done_callback = dma_done_callback};
    bsp_co5300_init(&display_info);
    printf("Display initialized (or crashed trying).\n");

    // --- SD CARD CODE --- (Removing test.txt logic)
    printf("Attempting to initialize SD card and mount filesystem...\n");
    if (!sd_init_driver())
    {
        printf("ERROR: SD card initialization failed. Halting.\n");
        while (true)
        {
            tight_loop_contents();
        }
    }
    printf("SD card driver initialized successfully.\n");
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
    printf("SD card mounted successfully.\n");
    // --- END OF SD CARD CODE ---

    printf("Setting up for animation with %d frames...\n", TOTAL_ANIMATION_FRAMES);

    int num_frames = TOTAL_ANIMATION_FRAMES; // Use the defined total

    if (num_frames == 0) // This condition might need adjustment if TOTAL_ANIMATION_FRAMES can be 0
    {
        printf("ERROR: TOTAL_ANIMATION_FRAMES is 0. Halting with error colors.\n");
        uint8_t error_colors[] = {RED_COLOR, BLUE_COLOR, GREEN_COLOR}; // Using new defines
        int error_color_index = 0;
        while (1)
        {
            bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1); // Full screen for error
            uint8_t error_line_buffer[DISPLAY_WIDTH];                           // For 8-bit
            for (int i = 0; i < DISPLAY_WIDTH; i++)
            {
                error_line_buffer[i] = error_colors[error_color_index];
            }
            for (int y = 0; y < DISPLAY_HEIGHT; y++) // Full height for error
            {
                while (!dma_transfer_complete)
                {
                    sleep_us(10);
                }
                dma_transfer_complete = false;
                bsp_co5300_flush(error_line_buffer, DISPLAY_WIDTH); // For 8-bit
            }
            error_color_index = (error_color_index + 1) % 3;
            sleep_ms(333);
        }
    }

    // Calculate padding for centering the frame
    const int PADDING_X = (DISPLAY_WIDTH - SCALED_FRAME_WIDTH) / 2;   // Centering the central scaled tile
    const int PADDING_Y = (DISPLAY_HEIGHT - SCALED_FRAME_HEIGHT) / 2; // Centering the central scaled tile

    // Pre-calculate grid boundaries
    const int GRID_LEFT = PADDING_X;
    const int GRID_RIGHT = PADDING_X + SCALED_FRAME_WIDTH;
    const int GRID_TOP = PADDING_Y;
    const int GRID_BOTTOM = PADDING_Y + SCALED_FRAME_HEIGHT;
    const int GRID_WIDTH = GRID_RIGHT - GRID_LEFT;
    const int GRID_HEIGHT = GRID_BOTTOM - GRID_TOP;

    // Create lookup tables for the grid area only
    static uint8_t source_x_lut[SCALED_FRAME_WIDTH];  // Just 1 tile wide now
    static uint8_t source_y_lut[SCALED_FRAME_HEIGHT]; // Just 1 tile tall now

    // Pre-compute X lookup table (for 1 tile horizontally)
    for (int x = 0; x < GRID_WIDTH; x++)
    {
        source_x_lut[x] = (x * FRAME_WIDTH) / SCALED_FRAME_WIDTH;
    }

    // Pre-compute Y lookup table (for 1 tile vertically)
    for (int y = 0; y < GRID_HEIGHT; y++)
    {
        source_y_lut[y] = (y * FRAME_HEIGHT) / SCALED_FRAME_HEIGHT;
    }

    // Pre-create a black line for fast memcpy
    static uint8_t black_line[DISPLAY_WIDTH];
    memset(black_line, 0x00, DISPLAY_WIDTH);

    // Main animation loop
    int current_frame_index = 0;

    // Line buffer for sending to display
    static uint8_t line_buffer[DISPLAY_WIDTH]; // Just one line

    // Multi-frame buffer system - simplified!
    static uint8_t frame_buffers[FRAMES_TO_BUFFER][FRAME_HEIGHT * FRAME_WIDTH];
    static int frame_indices[FRAMES_TO_BUFFER]; // Which frame number is in each buffer slot

    // Initialize all slots as empty
    for (int i = 0; i < FRAMES_TO_BUFFER; i++)
    {
        frame_indices[i] = -1;
    }

    FIL frame_fil;
    UINT bytes_read_for_full_frame;
    char current_frame_full_path[MAX_FILENAME_LEN + 8];

    // Pre-load initial frames
    printf("Pre-loading %d frames into RAM...\n", FRAMES_TO_BUFFER);
    for (int i = 0; i < FRAMES_TO_BUFFER && i < num_frames; i++)
    {
        snprintf(current_frame_full_path, sizeof(current_frame_full_path), "/output/snowman-%d.bin", i);
        fr = f_open(&frame_fil, current_frame_full_path, FA_READ);

        if (fr == FR_OK)
        {
            fr = f_read(&frame_fil, frame_buffers[i], FRAME_HEIGHT * FRAME_WIDTH, &bytes_read_for_full_frame);
            if (fr == FR_OK && bytes_read_for_full_frame == FRAME_HEIGHT * FRAME_WIDTH)
            {
                frame_indices[i] = i;
                printf("Pre-loaded frame %d into slot %d\n", i, i);
            }
            else
            {
                memset(frame_buffers[i], 0x00, FRAME_HEIGHT * FRAME_WIDTH);
            }
            f_close(&frame_fil);
        }
        else
        {
            memset(frame_buffers[i], 0x00, FRAME_HEIGHT * FRAME_WIDTH);
        }
    }

    printf("Starting animation loop with %d frames.\n", num_frames);

    int frames_displayed = 0;
    uint32_t start_time = to_ms_since_boot(get_absolute_time());

    while (1)
    {
        // Find which buffer slot has our current frame
        int buffer_slot = -1;
        for (int i = 0; i < FRAMES_TO_BUFFER; i++)
        {
            if (frame_indices[i] == current_frame_index)
            {
                buffer_slot = i;
                break;
            }
        }

        // If not found, load it immediately (shouldn't happen after initial load)
        if (buffer_slot == -1)
        {
            buffer_slot = current_frame_index % FRAMES_TO_BUFFER;
            snprintf(current_frame_full_path, sizeof(current_frame_full_path), "/gif-converter/output/snowman-%d.bin", current_frame_index);
            fr = f_open(&frame_fil, current_frame_full_path, FA_READ);
            if (fr == FR_OK)
            {
                f_read(&frame_fil, frame_buffers[buffer_slot], FRAME_HEIGHT * FRAME_WIDTH, &bytes_read_for_full_frame);
                f_close(&frame_fil);
            }
            else
            {
                memset(frame_buffers[buffer_slot], 0x00, FRAME_HEIGHT * FRAME_WIDTH);
            }
            frame_indices[buffer_slot] = current_frame_index;
        }

        // Use the buffered frame
        uint8_t *full_source_frame_buffer = frame_buffers[buffer_slot];

        // Send the frame line by line, building each line on the fly
        bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

        for (int y = 0; y < DISPLAY_HEIGHT; y++)
        {
            // Wait for any previous DMA to complete first
            while (!dma_transfer_complete)
            {
                sleep_us(10);
            }

            // Build one line
            if (y < GRID_TOP || y >= GRID_BOTTOM)
            {
                // Entire line is black - use pre-made black line
                dma_transfer_complete = false;
                bsp_co5300_flush(black_line, DISPLAY_WIDTH);
            }
            else
            {
                // Line has some content
                int grid_y = y - GRID_TOP;
                uint8_t source_y = source_y_lut[grid_y];
                uint8_t *source_row = &full_source_frame_buffer[source_y * FRAME_WIDTH];

                // Fill black on left
                if (GRID_LEFT > 0)
                {
                    memset(line_buffer, 0x00, GRID_LEFT);
                }

                // Fill content in middle using lookup table
                uint8_t *dest = &line_buffer[GRID_LEFT];
                for (int grid_x = 0; grid_x < GRID_WIDTH; grid_x++)
                {
                    *dest++ = source_row[source_x_lut[grid_x]];
                }

                // Fill black on right
                if (GRID_RIGHT < DISPLAY_WIDTH)
                {
                    memset(&line_buffer[GRID_RIGHT], 0x00, DISPLAY_WIDTH - GRID_RIGHT);
                }

                dma_transfer_complete = false;
                bsp_co5300_flush(line_buffer, DISPLAY_WIDTH);
            }
        }

        // Wait for the last line's DMA to complete
        while (!dma_transfer_complete)
        {
            sleep_us(10);
        }

        // Load the frame that will be needed FRAMES_TO_BUFFER frames from now
        int future_frame = (current_frame_index + FRAMES_TO_BUFFER) % num_frames;
        int slot_to_replace = future_frame % FRAMES_TO_BUFFER;

        // Only load if this slot doesn't already have the frame we need
        if (frame_indices[slot_to_replace] != future_frame)
        {
            snprintf(current_frame_full_path, sizeof(current_frame_full_path), "/gif-converter/output/snowman-%d.bin", future_frame);
            fr = f_open(&frame_fil, current_frame_full_path, FA_READ);
            if (fr == FR_OK)
            {
                f_read(&frame_fil, frame_buffers[slot_to_replace], FRAME_HEIGHT * FRAME_WIDTH, &bytes_read_for_full_frame);
                f_close(&frame_fil);
                frame_indices[slot_to_replace] = future_frame;
            }
        }

        current_frame_index = (current_frame_index + 1) % num_frames;
        frames_displayed++;

        // Print FPS every 100 frames
        if (frames_displayed % 100 == 0)
        {
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            uint32_t elapsed_ms = current_time - start_time;
            float fps = (frames_displayed * 1000.0f) / elapsed_ms;
            printf("FPS: %.2f (displayed %d frames in %u ms)\n", fps, frames_displayed, elapsed_ms);
        }
    }

    return 0;
}