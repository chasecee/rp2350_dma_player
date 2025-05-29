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

// Display dimensions
#define PHYSICAL_DISPLAY_WIDTH 466
#define PHYSICAL_DISPLAY_HEIGHT 466
#define DISPLAY_WIDTH FRAME_WIDTH   // Render target width is frame width for 1:1
#define DISPLAY_HEIGHT FRAME_HEIGHT // Render target height is frame height for 1:1
#define FRAME_WIDTH 288             // Updated for the new frame size
#define FRAME_HEIGHT 288            // Updated for the new frame size
#define RED_COLOR 0x00F8            // RGB565 red - Bytes swapped for CO5300
#define GREEN_COLOR 0xE007          // RGB565 green - Bytes swapped for CO5300
#define BLUE_COLOR 0x1F00           // RGB565 blue - Bytes swapped for CO5300
#define MAX_FILENAME_LEN 64
#define MAX_FRAMES 100 // Max number of frames we can list in the manifest

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
    printf("Initializing scaling maps (1:1 direct mapping)...\n");
    for (int i = 0; i < DISPLAY_HEIGHT; i++) // DISPLAY_HEIGHT is now FRAME_HEIGHT
    {
        source_y_map[i] = (i * FRAME_HEIGHT) / DISPLAY_HEIGHT; // Will resolve to i
    }
    for (int i = 0; i < DISPLAY_WIDTH; i++) // DISPLAY_WIDTH is now FRAME_WIDTH
    {
        source_x_map[i] = (i * FRAME_WIDTH) / DISPLAY_WIDTH; // Will resolve to i
    }
    printf("Scaling maps initialized for 1:1 mapping.\n");
}

// Function to clear the entire physical screen to black
void clear_entire_screen_to_black(void)
{
    printf("Clearing physical screen to black...\n");
    bsp_co5300_set_window(0, 0, PHYSICAL_DISPLAY_WIDTH - 1, PHYSICAL_DISPLAY_HEIGHT - 1);
    bsp_co5300_prepare_for_frame_pixels();

    uint16_t black_line[PHYSICAL_DISPLAY_WIDTH];
    for (int i = 0; i < PHYSICAL_DISPLAY_WIDTH; i++)
    {
        black_line[i] = 0x0000; // Black
    }

    for (int y = 0; y < PHYSICAL_DISPLAY_HEIGHT; y++)
    {
        while (!dma_transfer_complete)
        {
            sleep_us(10);
        }
        dma_transfer_complete = false;
        bsp_co5300_flush(black_line, PHYSICAL_DISPLAY_WIDTH);
    }
    while (!dma_transfer_complete)
    { // Wait for the last flush
        sleep_us(10);
    }
    bsp_co5300_finish_frame_pixels();
    printf("Physical screen cleared.\n");
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
    // Set system clock to 150 MHz for RP2350
    // Must be called before PLLs are initialized by other functions (e.g. stdio_init_all might init USB PLL)
    // However, for safety and common practice, it's often set very early.
    // Check RP2350 datasheet & SDK docs for precise timing if issues arise.
    set_sys_clock_khz(150000, true); // <<< ADD THIS LINE
    sleep_ms(2000);                  // Allow time for clock to stabilize and for serial connection
    printf("MINIMAL HELLO WORLD! Can you see me? (Attempting minimal display init)\n");

    // Initialize precomputed scaling maps
    init_scaling_maps();

    // Initialize Display (minimal parameters)
    printf("Initializing display (minimal parameters)...\n");
    bsp_co5300_info_t display_info = {
        .width = PHYSICAL_DISPLAY_WIDTH,   // Use physical dimensions for init
        .height = PHYSICAL_DISPLAY_HEIGHT, // Use physical dimensions for init
        .x_offset = 6,                     // Adjusted based on working LVGL example
        .y_offset = 0,
        .brightness = 95,
        .enabled_dma = true,
        .dma_flush_done_callback = dma_done_callback};
    bsp_co5300_init(&display_info);
    printf("Display initialized (or crashed trying).\n");

    // Clear the entire screen to black first
    clear_entire_screen_to_black();

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
            bsp_co5300_set_window(0, 0, PHYSICAL_DISPLAY_WIDTH - 1, PHYSICAL_DISPLAY_HEIGHT - 1); // Full physical screen
            uint16_t error_line_buffer[PHYSICAL_DISPLAY_WIDTH];                                   // Full physical width
            for (int i = 0; i < PHYSICAL_DISPLAY_WIDTH; i++)
            {
                error_line_buffer[i] = error_colors[error_color_index];
            }
            for (int y = 0; y < PHYSICAL_DISPLAY_HEIGHT; y++) // Full physical height
            {
                while (!dma_transfer_complete)
                {
                    sleep_us(10);
                }
                dma_transfer_complete = false;
                bsp_co5300_flush(error_line_buffer, PHYSICAL_DISPLAY_WIDTH); // Full physical width
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
// Define how many lines to buffer for each DMA transfer
#define LINES_PER_BUFFER 64
    uint16_t frame_line_buffer_a[DISPLAY_WIDTH * LINES_PER_BUFFER];
    uint16_t frame_line_buffer_b[DISPLAY_WIDTH * LINES_PER_BUFFER];
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

    // Calculate offsets for centering 156x156 content on 466x466 display
    const int x_offset_centered = (PHYSICAL_DISPLAY_WIDTH - DISPLAY_WIDTH) / 2;   // DISPLAY_WIDTH is 156
    const int y_offset_centered = (PHYSICAL_DISPLAY_HEIGHT - DISPLAY_HEIGHT) / 2; // DISPLAY_HEIGHT is 156

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
            bsp_co5300_set_window(x_offset_centered, y_offset_centered, x_offset_centered + DISPLAY_WIDTH - 1, y_offset_centered + DISPLAY_HEIGHT - 1);
            bsp_co5300_prepare_for_frame_pixels();

            display_render_start_time = get_absolute_time();

            cpu_buffer_ptr = frame_line_buffer_a;
            dma_buffer_ptr = frame_line_buffer_b;

            // Prime the first buffer for DMA (first LINES_PER_BUFFER display lines)
            display_prep_start_time = get_absolute_time();
            for (int line_in_chunk = 0; line_in_chunk < LINES_PER_BUFFER; ++line_in_chunk)
            {
                if (line_in_chunk < DISPLAY_HEIGHT)
                { // Ensure we don't read past display height
                    uint16_t current_source_y = source_y_map[line_in_chunk];
                    uint32_t source_row_offset = (uint32_t)current_source_y * FRAME_WIDTH;
                    uint32_t dest_row_offset = (uint32_t)line_in_chunk * DISPLAY_WIDTH;
                    for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
                    {
                        cpu_buffer_ptr[dest_row_offset + dx] = frame_buffers[display_buffer_idx][source_row_offset + source_x_map[dx]];
                    }
                }
                else
                { // Fill with black or a default color if past display height (for the chunk)
                    uint32_t dest_row_offset = (uint32_t)line_in_chunk * DISPLAY_WIDTH;
                    for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
                    {
                        cpu_buffer_ptr[dest_row_offset + dx] = 0; // Black
                    }
                }
            }
            display_prep_end_time = get_absolute_time();
            printf("Initial %d lines prep took %lld us\n", LINES_PER_BUFFER, absolute_time_diff_us(display_prep_start_time, display_prep_end_time));

            for (int y_start = 0; y_start < DISPLAY_HEIGHT; y_start += LINES_PER_BUFFER)
            {
                volatile uint16_t *temp_buf = dma_buffer_ptr;
                dma_buffer_ptr = cpu_buffer_ptr;
                cpu_buffer_ptr = temp_buf;

                while (!dma_transfer_complete)
                {
                    sleep_us(10);
                }
                dma_transfer_complete = false;
                // Calculate how many lines to actually send (can be less than LINES_PER_BUFFER at the end)
                int lines_to_send_this_iteration = (y_start + LINES_PER_BUFFER <= DISPLAY_HEIGHT) ? LINES_PER_BUFFER : (DISPLAY_HEIGHT - y_start);
                bsp_co5300_flush((uint16_t *)dma_buffer_ptr, DISPLAY_WIDTH * lines_to_send_this_iteration);

                // Prepare the next chunk of lines
                int next_y_start_for_prep = y_start + LINES_PER_BUFFER;
                if (next_y_start_for_prep < DISPLAY_HEIGHT)
                {
                    display_prep_start_time = get_absolute_time();
                    for (int line_in_chunk = 0; line_in_chunk < LINES_PER_BUFFER; ++line_in_chunk)
                    {
                        int current_display_y = next_y_start_for_prep + line_in_chunk;
                        if (current_display_y < DISPLAY_HEIGHT)
                        {
                            uint16_t current_source_y = source_y_map[current_display_y];
                            uint32_t source_row_offset = (uint32_t)current_source_y * FRAME_WIDTH;
                            uint32_t dest_row_offset = (uint32_t)line_in_chunk * DISPLAY_WIDTH;
                            for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
                            {
                                cpu_buffer_ptr[dest_row_offset + dx] = frame_buffers[display_buffer_idx][source_row_offset + source_x_map[dx]];
                            }
                        }
                        else
                        { // Fill with black if this part of chunk is beyond display height
                            uint32_t dest_row_offset = (uint32_t)line_in_chunk * DISPLAY_WIDTH;
                            for (int dx = 0; dx < DISPLAY_WIDTH; ++dx)
                            {
                                cpu_buffer_ptr[dest_row_offset + dx] = 0; // Black
                            }
                        }
                    }
                    display_prep_end_time = get_absolute_time();
                    if (y_start == 0) // Only print for the first "next" prep to avoid spam
                        printf("Subsequent %d lines prep took %lld us\n", LINES_PER_BUFFER, absolute_time_diff_us(display_prep_start_time, display_prep_end_time));
                }
            }

            while (!dma_transfer_complete)
            {
                sleep_us(10);
            }
            bsp_co5300_finish_frame_pixels();
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
            if (display_frame_idx == 0) // This signifies a completed playthrough
            {
                loop_end_time = get_absolute_time();
                printf("Profiling: SD Load: %lld us, Display Render: %lld us, Full Loop: %lld us\n",
                       absolute_time_diff_us(sd_load_start_time, sd_load_end_time),
                       absolute_time_diff_us(display_render_start_time, display_render_end_time),
                       absolute_time_diff_us(loop_start_time, loop_end_time));

                printf("Cycle complete. Pausing...\n");
                sleep_ms(2000);
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