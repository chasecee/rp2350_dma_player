#ifndef SD_LOADER_H
#define SD_LOADER_H

#include "pico/stdlib.h"
#include "ff.h"    // For FIL type
#include "debug.h" // Add debug macro header

// Configurable chunk size for reading from SD card
// Must be a multiple of 512 (SD card sector size)
// Increased to 48KB for better throughput
#define SD_READ_CHUNK_SIZE 49152  // 48KB to read full 156x156x2 frames in one shot!

// These dimensions must match those used by the display logic in main.c
#define FRAME_WIDTH 156  // Match main.c source frame size (back to 156 for 3x scaling)
#define FRAME_HEIGHT 156 // Match main.c source frame size (back to 156 for 3x scaling)

// Maximum number of frame filenames the manifest reader can handle
#define MAX_FRAMES_IN_MANIFEST 4000 // Updated to support 3.4k+ frames
#define MAX_FILENAME_LEN_SD 64      // Maximum length of each frame filename

// WARNING: Only one buffer is supported. All code must use index 0 for frame_buffers, buffer_ready, and target_frame_for_buffer.

// Buffers managed by sd_loader.c
extern uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];  // Changed from [1] to [2] for double buffering
extern volatile bool buffer_ready[2];  // Changed from [1] to [2]
extern volatile int target_frame_for_buffer[2];  // Changed from [1] to [2]

/**
 * @brief Initializes the SD card loader module.
 *
 * Only supports loading from a single contiguous frames.bin file.
 *
 * @param total_frames Total number of frames available (computed from frames.bin size)
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