#include <stdio.h>
#include <string.h> // For strcpy, etc.
#include "pico/stdlib.h"
#include "ff.h"             // FatFS library
#include "sd_card.h"        // SD card driver functions
#include "bsp_co5300.h"     // CO5300 display driver
#include "dma_config.h"     // Include the new DMA config header
#include "sd_loader.h"      // Include the new SD loader module
#include "display_module.h" // Include the new display module

// Display dimensions (assuming 466x466 based on README)
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define FRAME_WIDTH 156    // Updated for the new frame size
#define FRAME_HEIGHT 156   // Updated for the new frame size
#define RED_COLOR 0x00F8   // RGB565 red - Bytes swapped for CO5300
#define GREEN_COLOR 0xE007 // RGB565 green - Bytes swapped for CO5300
#define BLUE_COLOR 0x1F00  // RGB565 blue - Bytes swapped for CO5300
#define MAX_FILENAME_LEN 64
#define MAX_FRAMES 460 // Max number of frames we can list in the manifest

volatile bool dma_transfer_complete = true; // Flag for DMA completion (for display flushing)

// Declare buffer_ready and next_buffer at the top level - MOVED TO SD_LOADER
// static bool buffer_ready[2] = {false, false};
// static int next_buffer = 0;

// Callback function for DMA
void dma_done_callback(void)
{
    dma_transfer_complete = true; // Signal DMA completion
}

// Initialize pre-computed scaling tables
// static void init_scaling_tables(void)
// {
//     const int PADDING_X = (DISPLAY_WIDTH - SCALED_FRAME_WIDTH) / 2;
//     const int PADDING_Y = (DISPLAY_HEIGHT - SCALED_FRAME_HEIGHT) / 2;
//
//     // Pre-compute x scaling
//     for (int dx = 0; dx < DISPLAY_WIDTH; dx++)
//     {
//         bool in_horizontal_grid = (dx >= (PADDING_X - SCALED_FRAME_WIDTH) && dx < (PADDING_X + SCALED_FRAME_WIDTH * 2));
//         if (in_horizontal_grid)
//         {
//             int dx_relative_to_grid_start = dx - (PADDING_X - SCALED_FRAME_WIDTH);
//             int x_in_current_scaled_tile = dx_relative_to_grid_start % SCALED_FRAME_WIDTH;
//             x_scale_table[dx] = (x_in_current_scaled_tile * FRAME_WIDTH) / SCALED_FRAME_WIDTH;
//         }
//         else
//         {
//             x_scale_table[dx] = 0xFFFF; // Invalid marker
//         }
//     }
//
//     // Pre-compute y scaling
//     for (int dy = 0; dy < DISPLAY_HEIGHT; dy++)
//     {
//         bool in_vertical_grid = (dy >= (PADDING_Y - SCALED_FRAME_HEIGHT) && dy < (PADDING_Y + SCALED_FRAME_HEIGHT * 2));
//         if (in_vertical_grid)
//         {
//             int dy_relative_to_grid_start = dy - (PADDING_Y - SCALED_FRAME_HEIGHT);
//             int y_in_current_scaled_tile = dy_relative_to_grid_start % SCALED_FRAME_HEIGHT;
//             y_scale_table[dy] = (y_in_current_scaled_tile * FRAME_HEIGHT) / SCALED_FRAME_HEIGHT;
//         }
//         else
//         {
//             y_scale_table[dy] = 0xFFFF; // Invalid marker
//         }
//     }
//
//     // Pre-compute grid validity
//     for (int dx = 0; dx < DISPLAY_WIDTH; dx++)
//     {
//         for (int dy = 0; dy < DISPLAY_HEIGHT; dy++)
//         {
//             bool in_horizontal_grid = (dx >= (PADDING_X - SCALED_FRAME_WIDTH) && dx < (PADDING_X + SCALED_FRAME_WIDTH * 2));
//             bool in_vertical_grid = (dy >= (PADDING_Y - SCALED_FRAME_HEIGHT) && dy < (PADDING_Y + SCALED_FRAME_HEIGHT * 2));
//             grid_valid_table[dx][dy] = in_horizontal_grid && in_vertical_grid;
//         }
//     }
// }

// Callback function when a frame is loaded - REMOVED (handled by sd_loader)
// void frame_load_complete(void)
// {
//     buffer_ready[next_buffer] = true;
// }

// Function to display a test pattern (using new display module)
void display_test_pattern_main(void) // Renamed to avoid conflict if display_module had one
{
    printf("Displaying test pattern (via main)\n");
    uint16_t test_pattern_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT]; // Full frame buffer for simplicity

    // Create a simple pattern: top half red, bottom half blue
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            if (y < DISPLAY_HEIGHT / 2)
            {
                test_pattern_buffer[y * DISPLAY_WIDTH + x] = RED_COLOR;
            }
            else
            {
                test_pattern_buffer[y * DISPLAY_WIDTH + x] = BLUE_COLOR;
            }
        }
    }
    // This is not how display_module_render_frame expects data.
    // It expects a FRAME_WIDTH*FRAME_HEIGHT buffer and does scaling.
    // For a direct test pattern like this, we'd need a different display_module function
    // or draw directly to line buffers if display_module exposed that.
    // For now, let's adapt this to send a "frame" that results in the pattern.
    // This is a bit hacky for a test pattern.
    // A better test pattern would use the frame_buffers and scaling.

    // Simplified: Fill a frame buffer with a color and tell display_module to render it
    // This won't be the same red/blue pattern as before without more complex setup
    // that mimics frame_buffers structure.
    // For now, just display a single color frame via the module.
    uint16_t single_color_frame[FRAME_WIDTH * FRAME_HEIGHT];
    for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i)
        single_color_frame[i] = GREEN_COLOR;

    display_module_render_frame(single_color_frame); // Send it
    printf("Test pattern (single green frame) display attempted.\n");
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("MINIMAL HELLO WORLD! Can you see me?\n");

    // Initialize scaling tables - REMOVED
    // init_scaling_tables();

    // Initialize Display using the new module
    printf("Initializing display module...\n");
    display_module_init(display_module_dma_done_callback); // Pass the callback from display_module
    printf("Display module initialized (or crashed trying).\n");

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
    const char *paths_to_list[] = {"/", "/output", "/output/000"}; // MODIFIED
    int num_paths_to_list = sizeof(paths_to_list) / sizeof(paths_to_list[0]);

    for (int p_idx = 0; p_idx < num_paths_to_list; ++p_idx)
    {
        fr = f_opendir(&dir, paths_to_list[p_idx]);
        if (fr == FR_OK)
        {
            printf("Listing directory: %s\n", paths_to_list[p_idx]);
            while (1)
            {
                fr = f_readdir(&dir, &fno);
                if (fr != FR_OK || fno.fname[0] == 0)
                    break;
                printf("  %s%s\n", fno.fname, (fno.fattrib & AM_DIR) ? "/" : "");
            }
            f_closedir(&dir);
        }
        else
        {
            printf("Failed to open directory: %s (Error %d)\n", paths_to_list[p_idx], fr);
        }
    } // END OF MODIFIED SECTION

    printf("Finished SD card initialization. Entering main loop.\n");
    // --- END OF SD CARD CODE ---

    // Display a test pattern to verify display functionality
    // display_test_pattern_main(); // Can be enabled for testing

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
            {                                                                                 // Ensure it's not an empty line
                printf("Manifest entry %d: [%s]\n", num_frames, frame_filenames[num_frames]); // MODIFIED
                num_frames++;
            }
        }
        f_close(&manifest_fil);
        printf("Read %d frame(s) from manifest.\n", num_frames);
    }

    if (num_frames == 0)
    {
        printf("No frames loaded from manifest or manifest not found. Halting with error colors.\n");
        uint16_t error_colors[] = {RED_COLOR, BLUE_COLOR, GREEN_COLOR};
        int error_color_index = 0;
        // This error display logic will be simplified or moved.
        // For now, it cannot directly use display_module_render_frame as it bypasses sd_loader buffers.
        // A dedicated error display function in display_module might be better.
        // Or, have this main loop construct a simple error "frame" and use display_module_render_frame.

        // Temporary direct bsp_co5300 use for error screen - this needs fixing
        // if we want to strictly use display_module for all display access.
        // For now, let's assume this is acceptable as it's a fatal error state.
        bsp_co5300_info_t display_info; // Need to re-init or ensure it's still valid.
                                        // This is problematic. Better to have display_module_show_error_pattern(...).
                                        // For quick fix, let's try to make an error frame.
        uint16_t error_frame_content[FRAME_WIDTH * FRAME_HEIGHT];

        while (1)
        {
            // Fill the error_frame_content with a solid color
            for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i)
                error_frame_content[i] = error_colors[error_color_index];

            // Use display_module to show this "error frame"
            // This will scale the small error_frame_content to the full display.
            display_module_render_frame(error_frame_content);

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

    printf("Starting scaled animation loop with %d frames.\n", num_frames);

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

    while (1)
    {
        sd_loader_process();

        if (buffer_ready[display_buffer_idx] &&
            sd_loader_get_target_frame_for_buffer(display_buffer_idx) == display_frame_idx)
        {
            // Frame is ready in the expected buffer!
            // The actual frame_buffers are now globally accessible via sd_loader.h (if made extern)
            // or sd_loader provides a getter. sd_loader.c has `uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];`
            // We need access to this. Let's assume sd_loader.h will expose `extern uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];`

            // printf("Displaying frame %d from sd_loader buffer_idx %d\n", display_frame_idx, display_buffer_idx);
            display_module_render_frame(frame_buffers[display_buffer_idx]); // Pass pointer to the correct full frame buffer

            // Frame display complete. Mark buffer as consumed and set its next target.
            int next_frame_to_target_for_this_buffer = (display_frame_idx + 2) % num_frames;
            if (num_frames < 2) // If 0 or 1 frame, always target 0 (or -1 if 0 frames, but num_frames > 0 here)
                next_frame_to_target_for_this_buffer = (num_frames == 0) ? -1 : 0;

            // printf("Main: Displayed frame %d from buffer %d. Next target for this buffer: frame %d\n",
            //        display_frame_idx, display_buffer_idx, next_frame_to_target_for_this_buffer);
            sd_loader_mark_buffer_consumed(display_buffer_idx, next_frame_to_target_for_this_buffer);

            display_frame_idx = (display_frame_idx + 1) % num_frames;
            display_buffer_idx = 1 - display_buffer_idx;

            if (display_frame_idx == 0)
            {
                playthroughs_completed_for_current_cycle++;
                // printf("Playthrough %d completed.\n", playthroughs_completed_for_current_cycle);

                bool cycle_complete = false;
                // Simplified playthrough logic: play 2 times if few frames, 1 time if many.
                int plays_before_pause = (num_frames <= 25 && num_frames > 0) ? 2 : 1;

                if (playthroughs_completed_for_current_cycle >= plays_before_pause)
                {
                    printf("Cycle complete (%d plays of %d frames). Pausing...\n", playthroughs_completed_for_current_cycle, num_frames);
                    playthroughs_completed_for_current_cycle = 0;
                    sleep_ms(2000); // Pause for 2 seconds
                }
            }
        }
        else
        {
            // Expected buffer not ready or contains wrong frame.
            // sd_loader_process() will continue trying to load.
            sleep_us(100); // Small delay to yield for loading
        }
    } // end while(1)

    return 0; // Should not be reached
}