#include <stdio.h>
#include <string.h> // For strcpy, etc.
#include <math.h>   // For M_PI and cosf()
#include "pico/stdlib.h"
#include "ff.h"         // FatFS library
#include "sd_card.h"    // SD card driver functions
#include "bsp_co5300.h" // CO5300 display driver
#include "pico/time.h"  // For get_absolute_time, to_us_since_boot

// Display dimensions (assuming 466x466 based on README)
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define FRAME_WIDTH 156         // Updated for the new frame size
#define FRAME_HEIGHT 156        // Updated for the new frame size
#define SCALED_FRAME_WIDTH 246  // New: Apparent width of each tile
#define SCALED_FRAME_HEIGHT 246 // New: Apparent height of each tile

// Updated color definitions for 8-bit RGB332
#define RED_COLOR 0xE0   // Binary 11100000 (R:111, G:000, B:00)
#define GREEN_COLOR 0x1C // Binary 00011100 (R:000, G:111, B:00)
#define BLUE_COLOR 0x03  // Binary 00000011 (R:000, G:000, B:11)
#define BLACK_COLOR 0x00 // Binary 00000000
#define WHITE_COLOR 0xFF // Binary 11111111

#define MAX_FILENAME_LEN 64
#define MAX_FRAMES 470 // Max number of frames we can list in the manifest

// Glitch effect parameters
#define MAX_TARGET_GLITCH_PROBABILITY 0.005f     // Max probability for a new glitch block (0.0 to 1.0)
#define GLITCH_PROBABILITY_PERIOD_SECONDS 500.0f // Period for the probability sine wave
#define MAX_GLITCH_LENGTH (FRAME_WIDTH / 3)      // Max horizontal length of a glitch segment (user can change to DISPLAY_WIDTH for full width glitches)
#define MAX_GLITCH_OFFSET 4                      // Max horizontal offset for sourcing the glitched segment (pixels)
#define MAX_GLITCH_BLOCK_HEIGHT_LINES 8          // Max number of consecutive lines a glitch block can last

// Static state for an active glitch block
static int s_glitch_lines_remaining = 0;
static int s_glitch_start_cpu_block;
static int s_glitch_len_block;
static int s_glitch_start_dma_source_block;
static int s_glitch_offset_dma_block;

volatile bool dma_transfer_complete = true; // Flag for DMA completion

// Callback function for DMA, now potentially used
void dma_done_callback(void)
{
    dma_transfer_complete = true; // Signal DMA completion
}

// Helper function to apply a glitch if one is active or start a new one
static void apply_glitch_if_active(volatile uint8_t *cpu_buf, volatile uint8_t *dma_buf, float current_glitch_probability)
{
    if (s_glitch_lines_remaining > 0)
    {
        // Continue active glitch block
        for (int k = 0; k < s_glitch_len_block; ++k)
        {
            int dma_src_idx = s_glitch_start_dma_source_block + k + s_glitch_offset_dma_block;
            if (s_glitch_start_cpu_block + k < DISPLAY_WIDTH && dma_src_idx >= 0 && dma_src_idx < DISPLAY_WIDTH)
            {
                cpu_buf[s_glitch_start_cpu_block + k] = dma_buf[dma_src_idx];
            }
        }
        s_glitch_lines_remaining--;
    }
    else
    {
        // Try to start a new glitch block
        if (((float)rand() / RAND_MAX) < current_glitch_probability)
        {
            s_glitch_lines_remaining = rand() % MAX_GLITCH_BLOCK_HEIGHT_LINES + 1;
            s_glitch_len_block = rand() % MAX_GLITCH_LENGTH + 1;
            if (s_glitch_len_block > DISPLAY_WIDTH)
                s_glitch_len_block = DISPLAY_WIDTH; // Cap at display width

            s_glitch_start_cpu_block = rand() % (DISPLAY_WIDTH - s_glitch_len_block + 1);        // Ensure it fits
            s_glitch_start_dma_source_block = rand() % (DISPLAY_WIDTH - s_glitch_len_block + 1); // Ensure it fits
            s_glitch_offset_dma_block = (rand() % (2 * MAX_GLITCH_OFFSET + 1)) - MAX_GLITCH_OFFSET;

            // Apply the first line of the new glitch block
            for (int k = 0; k < s_glitch_len_block; ++k)
            {
                int dma_src_idx = s_glitch_start_dma_source_block + k + s_glitch_offset_dma_block;
                if (s_glitch_start_cpu_block + k < DISPLAY_WIDTH && dma_src_idx >= 0 && dma_src_idx < DISPLAY_WIDTH)
                {
                    cpu_buf[s_glitch_start_cpu_block + k] = dma_buf[dma_src_idx];
                }
            }
            s_glitch_lines_remaining--; // We just drew one line of it
        }
    }
}

int main()
{
    stdio_init_all();
    srand((unsigned int)to_us_since_boot(get_absolute_time()));     // Seed random number generator
    uint64_t start_time_us = to_us_since_boot(get_absolute_time()); // For glitch probability timing
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

    // --- SD CARD CODE ---
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
    FIL fil;
    const char *filename = "test.txt";
    printf("Attempting to open and read '%s'...\n", filename);
    fr = f_open(&fil, filename, FA_READ);
    if (fr != FR_OK)
    {
        printf("ERROR: Failed to open %s. FatFS error code: %d\n", filename, fr);
        while (true)
        {
            tight_loop_contents();
        }
    }
    else
    {
        printf("Successfully opened %s. Reading contents:\n---\n", filename);
        char buffer[128];
        while (f_gets(buffer, sizeof(buffer), &fil))
        {
            printf("%s", buffer);
        }
        printf("\n---\nFinished reading %s.\n", filename);
        fr = f_close(&fil);
        if (fr != FR_OK)
        {
            printf("ERROR: Failed to close %s. FatFS error code: %d\n", filename, fr);
        }
        else
        {
            printf("Successfully closed %s.\n", filename);
        }
    }
    printf("Finished SD card operations. Entering main loop.\n");
    // --- END OF SD CARD CODE ---

    printf("Attempting to load animation from manifest.txt...\n");

    char frame_filenames[MAX_FRAMES][MAX_FILENAME_LEN];
    int num_frames = 0;
    FIL manifest_fil;
    const char *manifest_filename = "/output/manifest.txt";

    fr = f_open(&manifest_fil, manifest_filename, FA_READ);
    if (fr != FR_OK)
    {
        printf("ERROR: Failed to open manifest file %s. FatFS error code: %d\n", manifest_filename, fr);
        // Fallback to error display
    }
    else
    {
        printf("Successfully opened %s. Reading frame list.\n", manifest_filename);
        while (f_gets(frame_filenames[num_frames], MAX_FILENAME_LEN, &manifest_fil) && num_frames < MAX_FRAMES)
        {
            // Remove newline characters if any
            frame_filenames[num_frames][strcspn(frame_filenames[num_frames], "\r\n")] = 0;
            if (strlen(frame_filenames[num_frames]) > 0)
            { // Ensure it's not an empty line
                printf("Found frame: %s\n", frame_filenames[num_frames]);
                num_frames++;
            }
        }
        f_close(&manifest_fil);
        printf("Read %d frame(s) from manifest.\n", num_frames);
    }

    if (num_frames == 0)
    {
        printf("No frames loaded from manifest or manifest not found. Halting with error colors.\n");
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

    // Main animation loop
    int current_frame_index = 0;
    int playthroughs_completed_for_current_cycle = 0; // Added for loop logic
    uint8_t frame_line_buffer_a[DISPLAY_WIDTH];       // For 8-bit
    uint8_t frame_line_buffer_b[DISPLAY_WIDTH];       // For 8-bit
    uint8_t frame_line_buffer_c[DISPLAY_WIDTH];       // For 8-bit
    volatile uint8_t *cpu_buffer_ptr;                 // For 8-bit
    volatile uint8_t *ready_buffer_ptr;               // For 8-bit
    volatile uint8_t *dma_buffer_ptr;                 // For 8-bit

    // Buffer to hold one entire source frame in RAM
    static uint8_t full_source_frame_buffer[FRAME_HEIGHT * FRAME_WIDTH]; // For 8-bit

    FIL frame_fil;
    UINT bytes_read_for_full_frame;
    char current_frame_full_path[MAX_FILENAME_LEN + 8];
    uint64_t read_start_us, read_end_us; // For profiling

    printf("Starting 3x3 tiled animation loop with %d frames (RAM buffered).\n", num_frames);
    while (1)
    {
        snprintf(current_frame_full_path, sizeof(current_frame_full_path), "/output/%s", frame_filenames[current_frame_index]);

        fr = f_open(&frame_fil, current_frame_full_path, FA_READ);
        bool frame_load_success = false;
        if (fr != FR_OK)
        {
            printf("ERROR: Failed to open frame file %s. Error: %d. Displaying black.\n", current_frame_full_path, fr);
            for (int i = 0; i < FRAME_HEIGHT * FRAME_WIDTH; ++i)
                full_source_frame_buffer[i] = 0x00; // Fill with black for uint8_t (RGB332 black)
        }
        else
        {
            // Read the entire frame into RAM buffer
            read_start_us = to_us_since_boot(get_absolute_time());
            fr = f_read(&frame_fil, full_source_frame_buffer, FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint8_t), &bytes_read_for_full_frame);
            read_end_us = to_us_since_boot(get_absolute_time());

            if (fr != FR_OK || bytes_read_for_full_frame != (FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint8_t)))
            {
                printf("ERROR: Failed to read full frame %s. Read %u bytes, Error: %d. (Read time: %llu us). Displaying black.\n", current_frame_full_path, bytes_read_for_full_frame, fr, read_end_us - read_start_us);
                for (int i = 0; i < FRAME_HEIGHT * FRAME_WIDTH; ++i)
                    full_source_frame_buffer[i] = 0x00; // Fill with black for uint8_t
            }
            else
            {
                frame_load_success = true;
                printf("Loaded frame %s in %llu us.\n", current_frame_full_path, read_end_us - read_start_us);
            }
            f_close(&frame_fil); // Close file once buffered
        }

        // --- DMA Triple Buffering and Tiling from RAM ---
        bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1); // Full screen

        cpu_buffer_ptr = frame_line_buffer_a;
        ready_buffer_ptr = frame_line_buffer_b;
        dma_buffer_ptr = frame_line_buffer_c;
        volatile uint8_t *temp_swap_ptr; // For swapping buffers

        // Prime the first buffer (line 0) into cpu_buffer_ptr, then move to ready_buffer_ptr
        for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
        {
            int source_x = -1, source_y = -1;
            // Check if current display pixel (dx, 0) is within the 3x3 tiled area of SCALED frames
            bool in_horizontal_grid = (dx >= (PADDING_X - SCALED_FRAME_WIDTH) && dx < (PADDING_X + SCALED_FRAME_WIDTH * 2));
            bool in_vertical_grid = (0 >= (PADDING_Y - SCALED_FRAME_HEIGHT) && 0 < (PADDING_Y + SCALED_FRAME_HEIGHT * 2));

            if (in_horizontal_grid && in_vertical_grid)
            {
                // Calculate position relative to the start of the 3x3 grid of scaled tiles
                int dx_relative_to_grid_start = dx - (PADDING_X - SCALED_FRAME_WIDTH);
                int dy_relative_to_grid_start = 0 - (PADDING_Y - SCALED_FRAME_HEIGHT);

                // Determine position within the current SCALED tile
                int x_in_current_scaled_tile = dx_relative_to_grid_start % SCALED_FRAME_WIDTH;
                int y_in_current_scaled_tile = dy_relative_to_grid_start % SCALED_FRAME_HEIGHT;

                // Scale down to find corresponding pixel in the original FRAME_WIDTH x FRAME_HEIGHT source
                source_x = (x_in_current_scaled_tile * FRAME_WIDTH) / SCALED_FRAME_WIDTH;
                source_y = (y_in_current_scaled_tile * FRAME_HEIGHT) / SCALED_FRAME_HEIGHT;

                // Boundary checks for safety, though scaling down should keep it within bounds if logic is correct
                if (source_x >= 0 && source_x < FRAME_WIDTH && source_y >= 0 && source_y < FRAME_HEIGHT)
                {
                    cpu_buffer_ptr[dx] = full_source_frame_buffer[source_y * FRAME_WIDTH + source_x];
                }
                else
                {
                    cpu_buffer_ptr[dx] = 0x00; // Black for uint8_t
                }
            }
            else
            {
                cpu_buffer_ptr[dx] = 0x00; // Black for outside grid
            }
        }
        // Line 0 is in cpu_buffer_ptr, make it ready
        temp_swap_ptr = cpu_buffer_ptr;
        cpu_buffer_ptr = ready_buffer_ptr;
        ready_buffer_ptr = temp_swap_ptr;
        // Now: ready_buffer_ptr has line 0. cpu_buffer_ptr is free (was frame_line_buffer_b). dma_buffer_ptr is free (frame_line_buffer_c).

        for (int y = 0; y < DISPLAY_HEIGHT; y++) // For each line on the DISPLAY
        {
            // Wait for the previous DMA transfer to complete
            while (!dma_transfer_complete) // This will be true after the first line
            {
                sleep_us(10);
            }

            // Swap dma_buffer_ptr with ready_buffer_ptr to get the line to display
            // The old dma_buffer_ptr becomes the new ready_buffer_ptr (which is now free)
            temp_swap_ptr = dma_buffer_ptr;
            dma_buffer_ptr = ready_buffer_ptr;
            ready_buffer_ptr = temp_swap_ptr;

            dma_transfer_complete = false;
            bsp_co5300_flush((uint8_t *)dma_buffer_ptr, DISPLAY_WIDTH); // Send the current line

            // Prepare the next line in cpu_buffer_ptr (if not the last line of display)
            if (y < DISPLAY_HEIGHT - 1)
            {
                int display_y_next = y + 1;
                for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
                {
                    int source_x = -1, source_y = -1;
                    // Check if current display pixel (dx, display_y_next) is within the 3x3 tiled area of SCALED frames
                    bool in_horizontal_grid = (dx >= (PADDING_X - SCALED_FRAME_WIDTH) && dx < (PADDING_X + SCALED_FRAME_WIDTH * 2));
                    bool in_vertical_grid = (display_y_next >= (PADDING_Y - SCALED_FRAME_HEIGHT) && display_y_next < (PADDING_Y + SCALED_FRAME_HEIGHT * 2));

                    if (in_horizontal_grid && in_vertical_grid)
                    {
                        // Calculate position relative to the start of the 3x3 grid of scaled tiles
                        int dx_relative_to_grid_start = dx - (PADDING_X - SCALED_FRAME_WIDTH);
                        int dy_relative_to_grid_start = display_y_next - (PADDING_Y - SCALED_FRAME_HEIGHT);

                        // Determine position within the current SCALED tile
                        int x_in_current_scaled_tile = dx_relative_to_grid_start % SCALED_FRAME_WIDTH;
                        int y_in_current_scaled_tile = dy_relative_to_grid_start % SCALED_FRAME_HEIGHT;

                        // Scale down to find corresponding pixel in the original FRAME_WIDTH x FRAME_HEIGHT source
                        source_x = (x_in_current_scaled_tile * FRAME_WIDTH) / SCALED_FRAME_WIDTH;
                        source_y = (y_in_current_scaled_tile * FRAME_HEIGHT) / SCALED_FRAME_HEIGHT;

                        // Boundary checks for safety
                        if (source_x >= 0 && source_x < FRAME_WIDTH && source_y >= 0 && source_y < FRAME_HEIGHT)
                        {
                            cpu_buffer_ptr[dx] = full_source_frame_buffer[source_y * FRAME_WIDTH + source_x];
                        }
                        else
                        {
                            cpu_buffer_ptr[dx] = 0x00; // Black for uint8_t
                        }
                    }
                    else
                    {
                        cpu_buffer_ptr[dx] = 0x00; // Black for outside grid
                    }
                }

                // --- Glitch Injection Logic --- (Applies to the line *about* to be prepared for DMA)
                uint64_t current_time_us_loop = to_us_since_boot(get_absolute_time());
                float elapsed_seconds_loop = (current_time_us_loop - start_time_us) / 1000000.0f;
                float current_prob_loop = (MAX_TARGET_GLITCH_PROBABILITY / 2.0f) * (1.0f - cosf(2.0f * M_PI * elapsed_seconds_loop / GLITCH_PROBABILITY_PERIOD_SECONDS));
                // Apply glitch to cpu_buffer_ptr, using dma_buffer_ptr (which has the line currently being displayed) as a potential source
                apply_glitch_if_active(cpu_buffer_ptr, dma_buffer_ptr, current_prob_loop);
                // --- End Glitch Injection Logic ---

                // The line in cpu_buffer_ptr is now ready. Swap it with ready_buffer_ptr (which is free).
                // The old ready_buffer_ptr becomes the new cpu_buffer_ptr for the next iteration.
                temp_swap_ptr = cpu_buffer_ptr;
                cpu_buffer_ptr = ready_buffer_ptr;
                ready_buffer_ptr = temp_swap_ptr;
            }
        }
        // Wait for the last line's DMA to complete before starting next frame
        while (!dma_transfer_complete)
        {
            sleep_us(10);
        }
        // --- End DMA Triple Buffering ---

        current_frame_index = (current_frame_index + 1) % num_frames;

        if (current_frame_index == 0)
        { // A full playthrough of the animation has completed
            playthroughs_completed_for_current_cycle++;

            bool cycle_complete = false;
            if (num_frames <= 25)
            { // Short animation
                if (playthroughs_completed_for_current_cycle >= 2)
                {
                    cycle_complete = true;
                }
            }
            else
            { // Long animation
                if (playthroughs_completed_for_current_cycle >= 1)
                {
                    cycle_complete = true;
                }
            }

            if (cycle_complete)
            {
                playthroughs_completed_for_current_cycle = 0; // Reset for next cycle
                sleep_ms(2000);                               // Pause only after the full cycle (1 or 2 playthroughs)
            }
        }
    }

    return 0;
}