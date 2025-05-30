#include "sd_loader.h"
#include "ff.h"             // For FatFs file operations
#include <stdio.h>          // For printf
#include <string.h>         // For snprintf
#include "hardware/timer.h" // For profiling (optional)
#include "dma_config.h"     // Use existing DMA infrastructure

// Frame dimensions (must match main.c or be passed in)
// Assuming FRAME_WIDTH and FRAME_HEIGHT are defined in sd_loader.h or globally
// For this example, let's ensure they are defined if not already
#ifndef FRAME_WIDTH
#define FRAME_WIDTH 156 // Example, ensure this matches your actual frame width
#endif
#ifndef FRAME_HEIGHT
#define FRAME_HEIGHT 156 // Example, ensure this matches your actual frame height
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

// SD_BUFFER_SIZE is the number of pixels, which is also the number of bytes for 8-bit data.
#define SD_BUFFER_SIZE (FRAME_HEIGHT * FRAME_WIDTH)
#define CHUNK_SIZE_BYTES SD_BUFFER_SIZE // Read the whole frame at once

// Define the actual storage for frame buffers and ready flags
// Aligned to 32-byte boundary for optimal DMA performance
__attribute__((aligned(32))) uint8_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];
volatile bool buffer_ready[2] = {false, false};
volatile int target_frame_for_buffer[2] = {-1, -1}; // -1 indicates no specific target initially

// Internal state for the loader
static struct
{
    int total_manifest_frames;
    int current_buffer_idx;        // Buffer index (0 or 1) currently being loaded into.
    int current_frame_to_load_idx; // Manifest index of the frame currently being loaded
    FIL current_file_handle;       // File handle for the frame being loaded
    uint32_t current_file_total_size_bytes;
    uint32_t current_file_offset; // Current read offset within the file
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
    printf("SD_LOADER: Initializing with %d frames.\n", total_frames);
    loader_state.total_manifest_frames = total_frames;
    if (total_frames == 0)
    {
        printf("SD_LOADER: No frames to load. Halting loader processing.\n");
        return;
    }

    // Reset buffer states
    buffer_ready[0] = false;
    buffer_ready[1] = false;

    // Set initial targets for double buffering
    target_frame_for_buffer[0] = 0; // First buffer targets frame 0
    target_frame_for_buffer[1] = 1; // Second buffer targets frame 1 (or 0 if only one frame)
    if (total_frames == 1)
    {
        target_frame_for_buffer[1] = 0; // Loop frame 0 if only one frame
    }

    // Reset loader state
    loader_state.file_is_open = false;
    loader_state.current_buffer_idx = -1;        // No buffer being loaded yet
    loader_state.current_frame_to_load_idx = -1; // No frame being loaded yet
    loader_state.current_file_offset = 0;
    loader_state.current_file_total_size_bytes = 0;

    // Initialize the shared DMA channel
    init_sd_dma();

    printf("SD_LOADER: Init complete. Buffer 0 target: %d, Buffer 1 target: %d\n",
           target_frame_for_buffer[0], target_frame_for_buffer[1]);
}

// Attempts to open the file designated by frame_idx_to_load for buffer_idx_to_load.
static bool open_file_if_needed(int buffer_idx_to_load, int frame_idx_to_load)
{
    // Add validation for frame index
    if (frame_idx_to_load < 0)
    {
        printf("ERROR: Invalid frame index %d, resetting to 0\n", frame_idx_to_load);
        frame_idx_to_load = 0;
    }

    // Ensure frame index is within bounds
    frame_idx_to_load = frame_idx_to_load % loader_state.total_manifest_frames;

    // Validate buffer index
    if (buffer_idx_to_load < 0 || buffer_idx_to_load > 1)
    {
        printf("ERROR: Invalid buffer index %d\n", buffer_idx_to_load);
        return false;
    }

    if (loader_state.file_is_open &&
        loader_state.current_buffer_idx == buffer_idx_to_load &&
        loader_state.current_frame_to_load_idx == frame_idx_to_load)
    {
        return true; // Already open for this target
    }

    // Close any previously open file
    if (loader_state.file_is_open)
    {
        f_close(&loader_state.current_file_handle);
        loader_state.file_is_open = false;
    }

    // Generate filename on demand
    char filename[MAX_FILENAME_LEN_SD];
    snprintf(filename, MAX_FILENAME_LEN_SD, "frame-%05d.bin", frame_idx_to_load);

    FRESULT fr = f_open(&loader_state.current_file_handle, filename, FA_READ);

    if (fr != FR_OK)
    {
        printf("ERROR: Failed to open file '%s' (FR: %d)\n", filename, fr);
        loader_state.file_is_open = false;
        return false;
    }

    loader_state.file_is_open = true;
    loader_state.current_buffer_idx = buffer_idx_to_load;
    loader_state.current_frame_to_load_idx = frame_idx_to_load;
    loader_state.current_file_offset = 0;
    loader_state.current_file_total_size_bytes = f_size(&loader_state.current_file_handle);

    if (loader_state.current_file_total_size_bytes != SD_BUFFER_SIZE)
    {
        printf("ERROR: File size mismatch. Expected %d, got %lu\n",
               SD_BUFFER_SIZE, loader_state.current_file_total_size_bytes);
        f_close(&loader_state.current_file_handle);
        loader_state.file_is_open = false;
        return false;
    }

    return true;
}

void sd_loader_process(void)
{
    if (loader_state.total_manifest_frames == 0)
        return;

    // If we're in the middle of loading a frame, continue with that
    if (loader_state.file_is_open && loader_state.current_buffer_idx != -1)
    {
        goto continue_current_load;
    }

    // Find a buffer that needs loading
    int buffer_to_process = -1;

    // First priority: buffer that's not ready and has a target
    if (!buffer_ready[0] && target_frame_for_buffer[0] >= 0)
    {
        buffer_to_process = 0;
    }
    else if (!buffer_ready[1] && target_frame_for_buffer[1] >= 0)
    {
        buffer_to_process = 1;
    }

    // Second priority: buffer that's ready but could be preloaded with next frame
    else if (buffer_ready[0] && target_frame_for_buffer[1] >= 0 &&
             ((target_frame_for_buffer[0] + 1) % loader_state.total_manifest_frames) == target_frame_for_buffer[1])
    {
        buffer_to_process = 1;
    }
    else if (buffer_ready[1] && target_frame_for_buffer[0] >= 0 &&
             ((target_frame_for_buffer[1] + 1) % loader_state.total_manifest_frames) == target_frame_for_buffer[0])
    {
        buffer_to_process = 0;
    }

    if (buffer_to_process == -1)
    {
        return; // Nothing to do
    }

    // Track start time for this frame load
    uint32_t start_time = to_ms_since_boot(get_absolute_time());

    // If we're seeing long load times, be more aggressive about preloading
    bool should_preload = (avg_load_time > 0 && avg_load_time > 15); // If loads take >15ms, preload aggressively

    // Try to open the file if needed
    if (!open_file_if_needed(buffer_to_process, target_frame_for_buffer[buffer_to_process]))
    {
        return;
    }

continue_current_load:
    // Calculate how many sectors we need to read
    UINT bytes_remaining = FRAME_SIZE - loader_state.current_file_offset;
    UINT sectors_to_read = (bytes_remaining + SECTOR_SIZE - 1) / SECTOR_SIZE;

    // If we're seeing good performance, read more sectors at once
    if (should_preload && sectors_to_read > 32)
    {
        sectors_to_read = 32; // Read larger chunks when performance is good
    }
    else if (sectors_to_read > 16)
    {
        sectors_to_read = 16; // Default to medium chunks
    }

    UINT bytes_to_read = sectors_to_read * SECTOR_SIZE;
    UINT bytes_read = 0;

    // Ensure our read is sector-aligned for best performance
    if (bytes_to_read > 0)
    {
        uint8_t *buffer_write_ptr = &frame_buffers[loader_state.current_buffer_idx][loader_state.current_file_offset];

        // Use f_read directly - the SD card library handles its own DMA internally
        FRESULT fr = f_read(&loader_state.current_file_handle,
                            buffer_write_ptr,
                            bytes_to_read,
                            &bytes_read);

        if (fr != FR_OK)
        {
            f_close(&loader_state.current_file_handle);
            loader_state.file_is_open = false;
            return;
        }

        loader_state.current_file_offset += bytes_read;

        // Check if we've completed loading the frame
        if (loader_state.current_file_offset >= FRAME_SIZE)
        {
            buffer_ready[loader_state.current_buffer_idx] = true;
            f_close(&loader_state.current_file_handle);
            loader_state.file_is_open = false;

            // Update timing statistics
            uint32_t end_time = to_ms_since_boot(get_absolute_time());
            uint32_t load_time = end_time - start_time;

            // Update running average
            if (frame_count < 10)
            {
                avg_load_time = ((avg_load_time * frame_count) + load_time) / (frame_count + 1);
                frame_count++;
            }
            else
            {
                avg_load_time = (avg_load_time * 9 + load_time) / 10; // Moving average
            }

            last_frame_load_time = load_time;

            // Immediately try to load next frame if we're performing well
            if (should_preload)
            {
                sd_loader_process();
            }
        }
    }
}

void sd_loader_mark_buffer_consumed(int buffer_idx, int next_target_frame)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return;

    buffer_ready[buffer_idx] = false;

    // Ensure we properly loop back to the start
    int new_target;
    if (next_target_frame >= loader_state.total_manifest_frames)
    {
        new_target = next_target_frame % loader_state.total_manifest_frames;
        printf("LOOPING: Frame %d -> %d\n", next_target_frame, new_target);
    }
    else
    {
        new_target = next_target_frame;
    }

    target_frame_for_buffer[buffer_idx] = new_target;

    // If the file currently open was for the buffer we just consumed, close it
    if (loader_state.file_is_open && loader_state.current_buffer_idx == buffer_idx)
    {
        f_close(&loader_state.current_file_handle);
        loader_state.file_is_open = false;
        loader_state.current_buffer_idx = -1;
        loader_state.current_frame_to_load_idx = -1;
    }
}

int sd_loader_get_target_frame_for_buffer(int buffer_idx)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return -1;
    return target_frame_for_buffer[buffer_idx];
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