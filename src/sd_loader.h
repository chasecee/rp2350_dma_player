#ifndef SD_LOADER_H
#define SD_LOADER_H

#include "pico/stdlib.h"
#include "ff.h"     // For FIL type
#include <string.h> // For snprintf

// Configurable chunk size for reading from SD card
// Should be a multiple of 512 for efficiency, e.g., 4096, 8192
#define SD_READ_CHUNK_SIZE (4 * 1024) // 4KB chunks

// Define frame dimensions here as the authoritative source
#define FRAME_WIDTH 156
#define FRAME_HEIGHT 156

// Maximum number of frame filenames the manifest reader in main.c can handle.
// This is used to size the pointer passed to sd_loader_init.
#define MAX_FRAMES 460      // Must match main.c
#define MAX_FILENAME_LEN 64 // Must match main.c

// Define the actual storage for frame buffers and ready flags
extern uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];
extern volatile bool buffer_ready[2]; // Make buffer_ready also accessible if main needs it (it does)

// Internal state for the loader

/**
 * @brief Initializes the SD card loader module.
 *
 * @param frame_filenames_ptr Pointer to the array of frame filenames.
 * @param num_total_frames Total number of frames available as per the manifest.
 */
void sd_loader_init(const char (*frame_filenames_ptr)[MAX_FILENAME_LEN], int num_total_frames);

/**
 * @brief Processes a chunk of SD card loading.
 * This function should be called repeatedly from the main application loop.
 * It attempts to load data into one of the frame buffers if it's not already full
 * and a loading operation isn't already complete for it.
 */
void sd_loader_process(void);

/**
 * @brief Gets the frame index that is currently targeted for loading or is loaded in the specified buffer.
 *
 * @param buffer_idx The buffer index (0 or 1).
 * @return The frame index (from the manifest list) targeted for this buffer. Returns -1 if invalid.
 */
int sd_loader_get_target_frame_for_buffer(int buffer_idx);

/**
 * @brief Marks a buffer as consumed (displayed) and sets its next target frame.
 * This tells the loader that the buffer is free to be overwritten with new frame data.
 *
 * @param buffer_idx The buffer index (0 or 1) that was consumed.
 * @param next_frame_to_target_for_this_buffer The frame index (from manifest) that this buffer should now load.
 */
void sd_loader_mark_buffer_consumed(int buffer_idx, int next_frame_to_target_for_this_buffer);

#endif // SD_LOADER_H 