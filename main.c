#include <stdio.h>
#include "pico/stdlib.h"
#include "ff.h"         // FatFS library
#include "sd_card.h"    // SD card driver functions
#include "bsp_co5300.h" // CO5300 display driver

// Display dimensions (assuming 466x466 based on README)
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define RED_COLOR 0x00F8   // RGB565 red - Bytes swapped for CO5300
#define GREEN_COLOR 0xE007 // RGB565 green - Bytes swapped for CO5300
#define BLUE_COLOR 0x1F00  // RGB565 blue - Bytes swapped for CO5300

volatile bool dma_transfer_complete = true; // Flag for DMA completion

// Callback function for DMA, now potentially used
void dma_done_callback(void)
{
    dma_transfer_complete = true; // Signal DMA completion
}

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
        .enabled_dma = false, // DMA DISABLED for this test
        .dma_flush_done_callback = dma_done_callback};
    bsp_co5300_init(&display_info);
    printf("Display initialized (or crashed trying).\n");

    // --- SD CARD CODE TEMPORARILY COMMENTED OUT ---
    // printf("Attempting to initialize SD card and mount filesystem...\n");
    // if (!sd_init_driver())
    // {
    //     printf("ERROR: SD card initialization failed. Halting.\n");
    //     while (true)
    //     {
    //         tight_loop_contents();
    //     }
    // }
    // printf("SD card driver initialized successfully.\n");
    // FATFS fs;
    // FRESULT fr = f_mount(&fs, "", 1);
    // if (fr != FR_OK)
    // {
    //     printf("ERROR: Failed to mount SD card. FatFS error code: %d\n", fr);
    //     while (true)
    //     {
    //         tight_loop_contents();
    //     }
    // }
    // printf("SD card mounted successfully.\n");
    // FIL fil;
    // const char *filename = "test.txt";
    // printf("Attempting to open and read '%s'...\n", filename);
    // fr = f_open(&fil, filename, FA_READ);
    // if (fr != FR_OK)
    // {
    //     printf("ERROR: Failed to open %s. FatFS error code: %d\n", filename, fr);
    //     while (true)
    //     {
    //         tight_loop_contents();
    //     }
    // }
    // else
    // {
    //     printf("Successfully opened %s. Reading contents:\n---\n", filename);
    //     char buffer[128];
    //     while (f_gets(buffer, sizeof(buffer), &fil))
    //     {
    //         printf("%s", buffer);
    //     }
    //     printf("\n---\nFinished reading %s.\n", filename);
    //     fr = f_close(&fil);
    //     if (fr != FR_OK)
    //     {
    //         printf("ERROR: Failed to close %s. FatFS error code: %d\n", filename, fr);
    //     }
    //     else
    //     {
    //         printf("Successfully closed %s.\n", filename);
    //     }
    // }
    // printf("Finished SD card operations. Entering main loop.\n");
    // --- END OF SD CARD CODE ---

    printf("If you see this, display init didn't fully crash. Entering main loop.\n");
    uint16_t colors[] = {RED_COLOR, GREEN_COLOR, BLUE_COLOR};
    int color_index = 0;

    while (1)
    {
        // Set window for the entire display once before line-by-line flush
        bsp_co5300_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

        // Prepare the line buffer with the current color
        uint16_t line_buffer[DISPLAY_WIDTH];
        for (int i = 0; i < DISPLAY_WIDTH; i++)
        {
            line_buffer[i] = colors[color_index];
        }

        // Flush line by line using DMA
        for (int y = 0; y < DISPLAY_HEIGHT; y++)
        {
            // Wait for previous DMA transfer to complete
            // while (!dma_transfer_complete)
            // {
            //     sleep_us(10); // Small delay while polling
            // }
            // dma_transfer_complete = false; // Reset flag for current transfer

            bsp_co5300_flush(line_buffer, DISPLAY_WIDTH);
        }

        if (colors[color_index] == RED_COLOR)
        {
            printf("Displaying RED\n");
        }
        else if (colors[color_index] == GREEN_COLOR)
        {
            printf("Displaying GREEN\n");
        }
        else if (colors[color_index] == BLUE_COLOR)
        {
            printf("Displaying BLUE\n");
        }

        color_index = (color_index + 1) % 3; // Cycle through R, G, B

        sleep_ms(1000);
        // tight_loop_contents(); // Not strictly needed if we are just sleeping
    }

    return 0;
}