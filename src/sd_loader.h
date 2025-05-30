#ifndef SD_LOADER_H
#define SD_LOADER_H

#include "pico/stdlib.h"
#include "ff.h" // For FIL type

// Configurable chunk size for reading from SD card
// Must be a multiple of 512 (SD card sector size)
// Increased to 32KB for better throughput
#define SD_READ_CHUNK_SIZE 32768

// These dimensions must match those used by the display logic in main.c
#define FRAME_WIDTH 156  // Match main.c source frame size
#define FRAME_HEIGHT 156 // Match main.c source frame size

// Maximum number of frame filenames the manifest reader can handle
#define MAX_FRAMES_IN_MANIFEST 4000 // Updated to support 3.4k+ frames
#define MAX_FILENAME_LEN_SD 64      // Maximum length of each frame filename

// Buffer for one full frame (8-bit pixels)
// Aligned to 32-byte boundary for optimal DMA performance
__attribute__((aligned(32))) extern uint8_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];

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