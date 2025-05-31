#include "sd_loader.h"
#include "ff.h"             // For FatFs file operations
#include <stdio.h>          // For printf
#include <string.h>         // For snprintf
#include "hardware/timer.h" // For profiling (optional)
#include "dma_config.h"     // Use existing DMA infrastructure
#include "debug.h"          // Add debug macro header

// Frame dimensions (must match main.c or be passed in)
// Assuming FRAME_WIDTH and FRAME_HEIGHT are defined in sd_loader.h or globally
// For this example, let's ensure they are defined if not already
#ifndef FRAME_WIDTH
#define FRAME_WIDTH 156 // Back to 156 for 3x integer scaling
#endif
#ifndef FRAME_HEIGHT
#define FRAME_HEIGHT 156 // Back to 156 for 3x integer scaling
#endif

// Diagnostic messages to check ffconf.h values during compilation of sd_loader.c
#if defined(FF_FS_MINIMIZE)
#if FF_FS_MINIMIZE == 0
#pragma message "COMPILING SD_LOADER.C: FF_FS_MINIMIZE is 0"
#elif FF_FS_MINIMIZE == 1
#pragma message "COMPILING SD_LOADER.C: FF_FS_MINIMIZE is 1"
#elif FF_FS_MINIMIZE == 2
#pragma message "COMPILING SD_LOADER.C: FF_FS_MINIMIZE is 2"
#elif FF_FS_MINIMIZE == 3
#pragma message "COMPILING SD_LOADER.C: FF_FS_MINIMIZE is 3"
#else
#pragma message "COMPILING SD_LOADER.C: FF_FS_MINIMIZE is some other value"
#endif
#else
#pragma message "COMPILING SD_LOADER.C: FF_FS_MINIMIZE is NOT DEFINED"
#endif

#if defined(FF_FS_READONLY)
#if FF_FS_READONLY == 0
#pragma message "COMPILING SD_LOADER.C: FF_FS_READONLY is 0"
#elif FF_FS_READONLY == 1
#pragma message "COMPILING SD_LOADER.C: FF_FS_READONLY is 1"
#else
#pragma message "COMPILING SD_LOADER.C: FF_FS_READONLY is some other value"
#endif
#else
#pragma message "COMPILING SD_LOADER.C: FF_FS_READONLY is NOT DEFINED"
#endif

// SD_BUFFER_SIZE is the number of pixels. For 16-bit data, this is FRAME_HEIGHT * FRAME_WIDTH words (uint16_t).
// The size in bytes will be FRAME_HEIGHT * FRAME_WIDTH * 2.
#define FRAME_SIZE_BYTES (FRAME_HEIGHT * FRAME_WIDTH * 2) // Size of one frame in bytes
#define CHUNK_SIZE_BYTES FRAME_SIZE_BYTES                 // Read the whole frame at once

// Define the actual storage for frame buffers and ready flags
// Aligned to 32-byte boundary for optimal DMA performance
__attribute__((aligned(32))) uint16_t frame_buffers[1][FRAME_HEIGHT * FRAME_WIDTH];
volatile bool buffer_ready[1] = {false};
volatile int target_frame_for_buffer[1] = {-1};

// Internal state for the loader
static struct
{
    int total_frames;
    int current_buffer_idx;
    int current_frame_to_load_idx;
    FIL frames_bin_handle; // Single open handle for frames.bin
    uint32_t current_file_offset;
    bool file_is_open;
} loader_state;

// Track timing for adaptive loading
static uint32_t last_frame_load_time = 0;
static uint32_t avg_load_time = 0;
static uint32_t frame_count = 0;

// DMA channel for SD card reads
// static uint dma_chan;
// static dma_channel_config dma_config;
// static void dma_complete_handler() { ... }

#define SECTOR_SIZE 512 // Standard SD card sector size
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT)
#define SECTORS_PER_FRAME ((FRAME_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE)

// Attempt to read multiple frames at once if they fit in our chunk size
#define MAX_FRAMES_PER_READ ((SD_READ_CHUNK_SIZE) / FRAME_SIZE)

void sd_loader_init(int total_frames)
{
    DBG_PRINTF("SD_LOADER: sd_loader_init called with %d frames\n", total_frames);
    DBG_PRINTF("SD_LOADER: Initializing with %d frames (single-bin mode, single buffer).\n", total_frames);
    loader_state.total_frames = total_frames;
    if (total_frames == 0)
    {
        DBG_PRINTF("SD_LOADER: No frames to load. Halting loader processing.\n");
        return;
    }
    buffer_ready[0] = false;
    target_frame_for_buffer[0] = 0;
    loader_state.file_is_open = false;
    loader_state.current_buffer_idx = -1;
    loader_state.current_frame_to_load_idx = -1;
    loader_state.current_file_offset = 0;
    // Open frames.bin ONCE and keep it open
    FRESULT fr = f_open(&loader_state.frames_bin_handle, "frames.bin", FA_READ);
    if (fr != FR_OK)
    {
        DBG_PRINTF("ERROR: Failed to open frames.bin (FR: %d)\n", fr);
        loader_state.file_is_open = false;
        return;
    }
    loader_state.file_is_open = true;
    // init_sd_dma(); // Assuming general DMA is initialized elsewhere if needed for SD f_read, not this module's specific DMA channel.
    DBG_PRINTF("SD_LOADER: Init complete. Buffer 0 target: %d\n", target_frame_for_buffer[0]);
}

static bool seek_and_prepare_frame(int frame_idx_to_load)
{
    if (frame_idx_to_load < 0)
        frame_idx_to_load = 0;
    frame_idx_to_load = frame_idx_to_load % loader_state.total_frames;

    if (!loader_state.file_is_open)
        return false;

    // Seek to the correct offset in frames.bin
    uint32_t offset = frame_idx_to_load * FRAME_SIZE_BYTES;
    FRESULT fr = f_lseek(&loader_state.frames_bin_handle, offset);
    if (fr != FR_OK)
    {
        DBG_PRINTF("ERROR: Failed to seek to frame %d (offset %lu) in frames.bin (FR: %d)\n", frame_idx_to_load, offset, fr);
        return false;
    }
    loader_state.current_buffer_idx = 0;
    loader_state.current_frame_to_load_idx = frame_idx_to_load;
    loader_state.current_file_offset = 0;
    return true;
}

void sd_loader_process(void)
{
    DBG_PRINTF("SD_LOADER: sd_loader_process called\n");
    if (frame_buffers == NULL)
    {
        DBG_PRINTF("ERROR: frame_buffers is NULL!\n");
        return;
    }
    if (loader_state.total_frames == 0)
        return;

    // If the single buffer is not ready and has a valid target frame
    if (!buffer_ready[0] && target_frame_for_buffer[0] >= 0)
    {
        if (loader_state.current_frame_to_load_idx != target_frame_for_buffer[0] || loader_state.current_buffer_idx != 0)
        {
            // Need to seek and prepare if not already set up for this frame, or if state is stale
            if (!seek_and_prepare_frame(target_frame_for_buffer[0]))
            {
                return; // Error already printed by seek_and_prepare_frame
            }
        }
    }
    else
    {
        // Buffer is ready, or has no valid target, or already processing target. Nothing to initiate now.
        return;
    }

    // At this point, current_buffer_idx should be 0, and current_frame_to_load_idx should match target_frame_for_buffer[0]
    // and current_file_offset should be 0 for a fresh read.

    if (loader_state.current_file_offset >= FRAME_SIZE_BYTES)
    {
        // This implies the frame was already loaded conceptually, mark ready if not
        if (!buffer_ready[0])
            buffer_ready[0] = true;
        loader_state.current_buffer_idx = -1; // Reset to allow re-evaluation next call
        loader_state.current_frame_to_load_idx = -1;
        return;
    }

    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    UINT bytes_to_read = FRAME_SIZE_BYTES - loader_state.current_file_offset;
    UINT bytes_read = 0;

    uint8_t *buffer_write_ptr_u8 = (uint8_t *)&frame_buffers[0][0] + loader_state.current_file_offset;
    FRESULT fr = f_read(&loader_state.frames_bin_handle, buffer_write_ptr_u8, bytes_to_read, &bytes_read);

    if (fr != FR_OK)
    {
        DBG_PRINTF("ERROR: SD_LOADER: Failed to read frame data (FR: %d) for B0, frame %d.\n",
                   fr, loader_state.current_frame_to_load_idx);
        buffer_ready[0] = false;
        target_frame_for_buffer[0] = -2; // Indicate error/stale target
        loader_state.current_buffer_idx = -1;
        loader_state.current_frame_to_load_idx = -1;
        return;
    }

    loader_state.current_file_offset += bytes_read;

    if (loader_state.current_file_offset >= FRAME_SIZE_BYTES)
    {
        int successfully_loaded_frame = loader_state.current_frame_to_load_idx;
        buffer_ready[0] = true;

        loader_state.current_buffer_idx = -1;
        loader_state.current_frame_to_load_idx = -1;

        uint32_t end_time = to_ms_since_boot(get_absolute_time());
        uint32_t load_time = end_time - start_time;
        last_frame_load_time = load_time;

        if (frame_count < 10)
        {
            avg_load_time = ((avg_load_time * frame_count) + load_time) / (frame_count + 1);
            frame_count++;
        }
        else
        {
            avg_load_time = (avg_load_time * 9 + load_time) / 10;
        }
        DBG_PRINTF("SD_LOADER: B0 loaded frame %d took %lu ms. Avg: %lu ms.\n",
                   successfully_loaded_frame, load_time, avg_load_time);
    }
    else if (bytes_read == 0 && bytes_to_read > 0)
    {
        DBG_PRINTF("WARN: SD_LOADER: Read 0 bytes for B0 frame %d when %u were expected. EOF? FR_CODE %d\n",
                   loader_state.current_frame_to_load_idx, bytes_to_read, fr);
        buffer_ready[0] = false;
        target_frame_for_buffer[0] = -2;
        loader_state.current_buffer_idx = -1;
        loader_state.current_frame_to_load_idx = -1;
    }
}

void sd_loader_mark_buffer_consumed(int buffer_idx, int next_target_frame)
{
    if (buffer_idx != 0)
        return; // Should not happen with single buffer

    buffer_ready[0] = false;
    int new_target = next_target_frame % loader_state.total_frames;
    target_frame_for_buffer[0] = new_target;
    // DBG_PRINTF("SD_LOADER: B0 consumed. Next target: %d\n", new_target);
}

int sd_loader_get_target_frame_for_buffer(int buffer_idx)
{
    if (buffer_idx != 0)
        return -1;
    return target_frame_for_buffer[0];
}

// --- Old sd_loader_get_frame_data implementation (kept for reference, but not used by main.c anymore) ---
// const uint16_t *sd_loader_get_frame_data(int frame_index, int *buffer_idx_used)
// {
//     // Simplified: This function would need to manage which buffer (0 or 1)
//     // holds the requested frame_index and ensure it's loaded.
//     // For the double-buffered player, main.c will directly access frame_buffers[X]
//     // after checking buffer_ready[X] and ensuring it has the correct target frame.
//     // This function as-is is not directly compatible with the proactive loading model.
//     if (frame_index < 0 || frame_index >= num_frame_files)
//     {
//         return NULL;
//     }

//     // This is a placeholder and needs a proper double-buffering strategy
//     // For now, just load into buffer 0
//     // Construct full path
//     char full_path[MAX_FILENAME_LEN + 8]; // "output/" + filename
//     strcpy(full_path, "output/");
//     strcat(full_path, frame_files[frame_index]);

//     FIL fil;
//     FRESULT fr = f_open(&fil, full_path, FA_READ);
//     if (fr != FR_OK)
//     {
//         printf("Error opening frame file %s: %d\n", full_path, fr);
//         return NULL;
//     }

//     UINT bytes_read;
//     fr = f_read(&fil, frame_buffers[0], FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint16_t), &bytes_read);
//     f_close(&fil);

//     if (fr != FR_OK || bytes_read != FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint16_t))
//     {
//         printf("Error reading frame file %s. Read %u bytes, FR: %d\n", full_path, bytes_read, fr);
//         return NULL;
//     }

//     *buffer_idx_used = 0;
//     return frame_buffers[0];
// }