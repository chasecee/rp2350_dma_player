#ifndef SD_LOADER_H
#define SD_LOADER_H

#include "pico/stdlib.h"
#include "ff.h" // For FIL type

// Configurable chunk size for reading from SD card
// Should be a multiple of 512 for efficiency, e.g., 4096, 8192
// #define SD_READ_CHUNK_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2) // Full frame size (16-bit pixels)
#define SD_READ_CHUNK_SIZE 16384 // A more typical chunk size

// These dimensions must match those used by the display logic in main.c
// If main.c defines them, this module could extern them, or they can be duplicated.
// For now, defining them here for clarity within the loader's scope of managing these buffers.
#define FRAME_WIDTH 156  // Corrected to match actual BIN file size
#define FRAME_HEIGHT 156 // Corrected to match actual BIN file size

// Maximum number of frame filenames the manifest reader can handle.
// Must match MAX_FRAMES in main.c
#define MAX_FRAMES_IN_MANIFEST 4000 // Updated to support 3.4k+ frames
#define MAX_FILENAME_LEN_SD 64      // Maximum length of each frame filename

// Buffer for one full frame (16-bit RGB565 pixels)
// extern uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH]; // Old 16-bit
// extern uint8_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH]; // New 8-bit
extern uint8_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH]; // Changed to uint8_t

// Flags indicating if a buffer contains a fully loaded, ready-to-display frame
extern volatile bool buffer_ready[2];
// Target frame index for each buffer (-1 if no target)
extern volatile int target_frame_for_buffer[2];

/**
 * @brief Initializes the SD card loader module.
 *
 * @param total_frames Total number of frames available
 */
void sd_loader_init(int total_frames);

/**
 * @brief Processes a chunk of SD card loading.
 * Call repeatedly from main loop to load frames into available buffers.
 * Uses double-buffering to load next frame while current frame is displaying.
 */
void sd_loader_process(void);

/**
 * @brief Gets the frame index targeted for the specified buffer.
 *
 * @param buffer_idx The buffer index (0 or 1)
 * @return Frame index or -1 if invalid/no target
 */
int sd_loader_get_target_frame_for_buffer(int buffer_idx);

/**
 * @brief Marks a buffer as consumed and sets its next target frame.
 * Call after displaying a frame to allow buffer reuse.
 *
 * @param buffer_idx The consumed buffer (0 or 1)
 * @param next_target_frame Next frame to load into this buffer
 */
void sd_loader_mark_buffer_consumed(int buffer_idx, int next_target_frame);

#endif // SD_LOADER_H