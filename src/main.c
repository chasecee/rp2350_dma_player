#include <stdio.h>
#include <string.h> // For strcpy, etc.
#include "pico/stdlib.h"
#include "pico/time.h"  // Added for profiling
#include "ff.h"         // FatFS library
#include "sd_card.h"    // SD card driver functions
#include "bsp_co5300.h" // CO5300 display driver
#include "dma_config.h" // Include the new DMA config header
#include "sd_loader.h"  // Include the new SD loader module

// Display dimensions (assuming 466x466 based on README)
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define FRAME_WIDTH 156    // Updated for the new frame size
#define FRAME_HEIGHT 156   // Updated for the new frame size
#define RED_COLOR 0x00F8   // RGB565 red - Bytes swapped for CO5300
#define GREEN_COLOR 0xE007 // RGB565 green - Bytes swapped for CO5300
#define BLUE_COLOR 0x1F00  // RGB565 blue - Bytes swapped for CO5300
#define MAX_FILENAME_LEN 64
#define MAX_FRAMES 500 // Max number of frames we can list in the manifest

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
    printf("Initializing scaling maps...\n");
    for (int i = 0; i < DISPLAY_HEIGHT; i++)
    {
        source_y_map[i] = (i * FRAME_HEIGHT) / DISPLAY_HEIGHT;
    }
    for (int i = 0; i < DISPLAY_WIDTH; i++)
    {
        source_x_map[i] = (i * FRAME_WIDTH) / DISPLAY_WIDTH;
    }
    printf("Scaling maps initialized.\n");
}

// Function to display a test pattern
void display_test_pattern(void)
{
    printf("Displaying test pattern\n");
    bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    uint16_t test_pattern[DISPLAY_WIDTH];
    for (int i = 0; i < DISPLAY_WIDTH; i++)
    {
        test_pattern[i] = (i % 2 == 0) ? RED_COLOR : BLUE_COLOR;
    }
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        while (!dma_transfer_complete)
        {
            sleep_us(10);
        }
        dma_transfer_complete = false;
        bsp_co5300_flush(test_pattern, DISPLAY_WIDTH);
    }
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("MINIMAL HELLO WORLD! Can you see me? (Attempting minimal display init)\n");

    // Initialize precomputed scaling maps
    init_scaling_maps();

    // Initialize Display (minimal parameters)
    printf("Initializing display (minimal parameters)...\n");
    bsp_co5300_info_t display_info = {
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
        .x_offset = 6, // Adjusted based on working LVGL example
        .y_offset = 0,
        .brightness = 95,
        .enabled_dma = true,
        .dma_flush_done_callback = dma_done_callback};
    bsp_co5300_init(&display_info);
    printf("Display initialized (or crashed trying).\n");

    // Initialize SD DMA (must be done after display potentially claims DMA_IRQ_0)
    printf("Initializing SD DMA...\n");
    init_sd_dma();
    printf("SD DMA initialized.\n");

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

    // Debug: List root directory contents
    DIR dir;
    FILINFO fno;
    fr = f_opendir(&dir, "");
    if (fr == FR_OK)
    {
        printf("Root directory contents:\n");
        while (1)
        {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0)
                break;
            printf("  %s%s\n", fno.fname, (fno.fattrib & AM_DIR) ? "/" : "");
        }
        f_closedir(&dir);
    }

    printf("Finished SD card initialization. Entering main loop.\n");
    // --- END OF SD CARD CODE ---

    // Display a test pattern to verify display functionality
    // display_test_pattern(); // Can be enabled for testing

    printf("Attempting to load animation from manifest.txt...\n");

    char frame_filenames[MAX_FRAMES][MAX_FILENAME_LEN];
    int num_frames = 0;
    FIL manifest_fil;
    const char *manifest_filename = "output/manifest.txt";

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
        uint16_t error_colors[] = {RED_COLOR, BLUE_COLOR, GREEN_COLOR}; // Added green for manifest error
        int error_color_index = 0;
        while (1)
        {
            bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1); // Full screen for error
            uint16_t error_line_buffer[DISPLAY_WIDTH];                          // Full width for error
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
                bsp_co5300_flush(error_line_buffer, DISPLAY_WIDTH); // Full width for error
            }
            error_color_index = (error_color_index + 1) % 3;
            sleep_ms(333);
        }
    }

    // Initialize the SD Loader module
    sd_loader_init(frame_filenames, num_frames);

    // Buffer to hold two entire source frames in RAM for double buffering - MOVED TO SD_LOADER
    // static uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH]; // THIS LINE SHOULD REMAIN COMMENTED OR BE DELETED
    // int current_buffer = 0; // Will be replaced by display_buffer_idx_for_display

    // DMA line buffers (for display)
    uint16_t frame_line_buffer_a[DISPLAY_WIDTH];
    uint16_t frame_line_buffer_b[DISPLAY_WIDTH];
    volatile uint16_t *cpu_buffer_ptr;
    volatile uint16_t *dma_buffer_ptr;

    // FIL frame_fil; // Moved to sd_loader.c
    // UINT bytes_read_for_full_frame; // Not needed with chunked loading this way
    // char current_frame_full_path[MAX_FILENAME_LEN + 8]; // Handled by sd_loader

    printf("Starting scaled animation loop with %d frames (chunked loading).\n", num_frames);

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
    int playthroughs_completed_for_current_cycle = 0;

    printf("Entering main display & loader loop\n");

    absolute_time_t loop_start_time, loop_end_time;
    absolute_time_t sd_load_start_time, sd_load_end_time;
    absolute_time_t display_prep_start_time, display_prep_end_time;
    absolute_time_t display_render_start_time, display_render_end_time;

    while (1)
    {
        loop_start_time = get_absolute_time();

        // Process SD card loading - this will load chunks into buffers if needed
        sd_load_start_time = get_absolute_time();
        sd_loader_process();
        sd_load_end_time = get_absolute_time();

        // Check if the buffer we expect to display from is ready and contains the correct frame
        if (buffer_ready[display_buffer_idx] &&
            sd_loader_get_target_frame_for_buffer(display_buffer_idx) == display_frame_idx)
        {
            // printf("Displaying frame %d from buffer_idx %d\n", display_frame_idx, display_buffer_idx);
            bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

            display_render_start_time = get_absolute_time();

            cpu_buffer_ptr = frame_line_buffer_a;
            dma_buffer_ptr = frame_line_buffer_b;

            // Prime the first buffer for DMA (display line 0)
            // Scaled drawing: for display line y=0
            display_prep_start_time = get_absolute_time();
            uint16_t current_source_y_for_prime = source_y_map[0];
            uint32_t source_row_offset_for_prime = (uint32_t)current_source_y_for_prime * FRAME_WIDTH;
            for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
            {
                cpu_buffer_ptr[dx] = frame_buffers[display_buffer_idx][source_row_offset_for_prime + source_x_map[dx]];
            }
            display_prep_end_time = get_absolute_time();
            printf("Initial line prep took %lld us\n", absolute_time_diff_us(display_prep_start_time, display_prep_end_time));

            for (int y = 0; y < DISPLAY_HEIGHT; y++)
            {
                volatile uint16_t *temp_buf = dma_buffer_ptr;
                dma_buffer_ptr = cpu_buffer_ptr;
                cpu_buffer_ptr = temp_buf;

                while (!dma_transfer_complete)
                {
                    // tight_loop_contents(); // prefer sleep_us for potentially yielding
                    sleep_us(10);
                }
                dma_transfer_complete = false;
                bsp_co5300_flush((uint16_t *)dma_buffer_ptr, DISPLAY_WIDTH);

                if (y < DISPLAY_HEIGHT - 1)
                {
                    int display_y_next = y + 1;
                    // Scaled drawing for the next line
                    display_prep_start_time = get_absolute_time();
                    uint16_t current_source_y_for_next = source_y_map[display_y_next];
                    uint32_t source_row_offset_for_next = (uint32_t)current_source_y_for_next * FRAME_WIDTH;
                    for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
                    {
                        cpu_buffer_ptr[dx] = frame_buffers[display_buffer_idx][source_row_offset_for_next + source_x_map[dx]];
                    }
                    display_prep_end_time = get_absolute_time();
                    if (y == 0)
                        printf("Subsequent line prep took %lld us\n", absolute_time_diff_us(display_prep_start_time, display_prep_end_time));
                }
            }

            while (!dma_transfer_complete)
            {
                // tight_loop_contents();
                sleep_us(10);
            }
            display_render_end_time = get_absolute_time();

            // Frame display complete. Mark buffer as consumed and set its next target.
            int next_frame_to_target_for_this_buffer = (display_frame_idx + 2) % num_frames;
            if (num_frames == 1)
                next_frame_to_target_for_this_buffer = 0; // Special case for single frame

            // printf("Main: Displayed frame %d from buffer %d. Next target for this buffer: frame %d\n",
            //        display_frame_idx, display_buffer_idx, next_frame_to_target_for_this_buffer);
            sd_loader_mark_buffer_consumed(display_buffer_idx, next_frame_to_target_for_this_buffer);

            // Advance to next frame and buffer for display
            display_frame_idx = (display_frame_idx + 1) % num_frames;
            display_buffer_idx = 1 - display_buffer_idx; // Flip to other buffer

            // Playthrough logic
            if (display_frame_idx == 0)
            {
                playthroughs_completed_for_current_cycle++;
                // printf("Playthrough %d completed.\n", playthroughs_completed_for_current_cycle);
                loop_end_time = get_absolute_time();
                printf("Profiling: SD Load: %lld us, Display Render: %lld us, Full Loop: %lld us\n",
                       absolute_time_diff_us(sd_load_start_time, sd_load_end_time),
                       absolute_time_diff_us(display_render_start_time, display_render_end_time),
                       absolute_time_diff_us(loop_start_time, loop_end_time));

                bool cycle_complete = false;
                if (num_frames <= 25)
                {
                    if (playthroughs_completed_for_current_cycle >= 2)
                    {
                        cycle_complete = true;
                    }
                }
                else
                {
                    if (playthroughs_completed_for_current_cycle >= 1)
                    {
                        cycle_complete = true;
                    }
                }

                if (cycle_complete)
                {
                    printf("Cycle complete. Pausing...\n");
                    playthroughs_completed_for_current_cycle = 0;
                    sleep_ms(2000);
                }
            }
        }
        else
        {
            // Expected buffer not ready or contains wrong frame, or still loading.
            // sd_loader_process() will continue trying to load.
            // We can add a small delay here if the display loop is too tight
            // and not giving enough time for sd_loader_process to work effectively,
            // though sd_loader_process itself uses blocking f_read for chunks.
            // tight_loop_contents();
            sleep_us(100); // Small delay if not displaying, to yield a bit for loading
            // If not displaying, still print SD load time and a marker for loop time if needed
            loop_end_time = get_absolute_time();
            if (!(buffer_ready[display_buffer_idx] && sd_loader_get_target_frame_for_buffer(display_buffer_idx) == display_frame_idx))
            {
                printf("Profiling (No Display): SD Load: %lld us, Loop: %lld us\n",
                       absolute_time_diff_us(sd_load_start_time, sd_load_end_time),
                       absolute_time_diff_us(loop_start_time, loop_end_time));
            }
        }
    } // end while(1)

    return 0;
}