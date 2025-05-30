#include "sd_loader.h"
#include "ff.h"             // For FatFs file operations
#include <stdio.h>          // For printf
#include <string.h>         // For snprintf
#include "hardware/timer.h" // For profiling (optional)

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
// These are defined as extern in sd_loader.h and used by main.c for display
uint8_t frame_buffers[2][SD_BUFFER_SIZE]; // Changed to uint8_t
volatile bool buffer_ready[2] = {false, false};
volatile int target_frame_for_buffer[2] = {-1, -1}; // -1 indicates no specific target initially

// Internal state for the loader
static struct
{
    char manifest_frame_filenames[MAX_FRAMES_IN_MANIFEST][MAX_FILENAME_LEN_SD];
    int total_manifest_frames;
    int current_buffer_idx;                                   // Buffer index (0 or 1) currently being loaded into.
    int current_frame_to_load_idx;                            // Manifest index of the frame currently being loaded or targeted for the current_buffer_idx.
    FIL current_file_handle;                                  // File handle for the frame being loaded
    char current_filename_full_path[MAX_FILENAME_LEN_SD + 8]; // "output/" + filename
    uint32_t current_file_total_size_bytes;
    uint32_t current_file_offset; // Current read offset within the file
    bool file_is_open;
    // bool initial_load_for_buffer0_done; // Not strictly needed with new logic
    // bool initial_load_for_buffer1_done;
} loader_state;

void sd_loader_init(char frame_filenames[][MAX_FILENAME_LEN_SD], int num_files)
{
    printf("SD_LOADER: Initializing with %d files.\\n", num_files);
    loader_state.total_manifest_frames = num_files;
    if (num_files == 0)
    {
        printf("SD_LOADER: No files to load. Halting loader processing.\\n");
        return;
    }

    for (int i = 0; i < num_files && i < MAX_FRAMES_IN_MANIFEST; ++i)
    {
        strncpy(loader_state.manifest_frame_filenames[i], frame_filenames[i], MAX_FILENAME_LEN_SD - 1);
        loader_state.manifest_frame_filenames[i][MAX_FILENAME_LEN_SD - 1] = '\0'; // Ensure null termination
    }

    // Initial targets
    target_frame_for_buffer[0] = 0;
    if (num_files > 1)
    {
        target_frame_for_buffer[1] = 1;
    }
    else
    {
        target_frame_for_buffer[1] = 0; // Loop frame 0 if only one frame
    }

    loader_state.file_is_open = false;
    loader_state.current_buffer_idx = -1; // No buffer actively being written to by the loader initially
    loader_state.current_frame_to_load_idx = -1;
    loader_state.current_file_offset = 0;
    loader_state.current_file_total_size_bytes = 0;
    buffer_ready[0] = false;
    buffer_ready[1] = false;

    printf("SD_LOADER: Init complete. Buffer 0 targeting frame %d. Buffer 1 targeting frame %d.\\n", target_frame_for_buffer[0], target_frame_for_buffer[1]);
}

// Attempts to open the file designated by frame_idx_to_load for buffer_idx_to_load.
// Sets loader_state.current_buffer_idx and loader_state.current_frame_to_load_idx.
static bool open_file_if_needed(int buffer_idx_to_load, int frame_idx_to_load)
{
    printf("OPEN_FILE_IF_NEEDED_ENTRY [%llu]: buf_idx=%d, frame_idx=%d\n", time_us_64(), buffer_idx_to_load, frame_idx_to_load);

    if (loader_state.file_is_open &&
        loader_state.current_buffer_idx == buffer_idx_to_load &&
        loader_state.current_frame_to_load_idx == frame_idx_to_load)
    {
        return true; // Already open for this target
    }

    if (loader_state.file_is_open)
    { // File is open, but for a different target or buffer
        f_close(&loader_state.current_file_handle);
        loader_state.file_is_open = false;
    }

    if (frame_idx_to_load < 0 || frame_idx_to_load >= loader_state.total_manifest_frames)
    {
        return false;
    }

    // Construct full path: filename directly in root
    strncpy(loader_state.current_filename_full_path, loader_state.manifest_frame_filenames[frame_idx_to_load],
            MAX_FILENAME_LEN_SD + 8 - 1);
    loader_state.current_filename_full_path[MAX_FILENAME_LEN_SD + 8 - 1] = '\0'; // Ensure null termination

    FRESULT fr = f_open(&loader_state.current_file_handle, loader_state.current_filename_full_path, FA_READ);

    if (fr != FR_OK)
    {
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
        f_close(&loader_state.current_file_handle);
        loader_state.file_is_open = false;
        return false;
    }
    return true;
}

void sd_loader_process(void)
{
    printf("SD_LOADER_PROCESS START [%llu]: file_open=%d, cur_buf=%d, cur_load_idx=%d, offset=%lu. B0_ready:%d,T0:%d. B1_ready:%d,T1:%d\n",
           time_us_64(), loader_state.file_is_open, loader_state.current_buffer_idx, loader_state.current_frame_to_load_idx, loader_state.current_file_offset,
           buffer_ready[0], target_frame_for_buffer[0], buffer_ready[1], target_frame_for_buffer[1]);

    if (loader_state.total_manifest_frames == 0)
        return;

    int buffer_to_process = -1;

    // Prioritize buffer 0 if it needs loading
    if (!buffer_ready[0] && target_frame_for_buffer[0] != -1)
    {
        buffer_to_process = 0;
    }
    // Else, try buffer 1 if it needs loading
    else if (!buffer_ready[1] && target_frame_for_buffer[1] != -1)
    {
        buffer_to_process = 1;
    }

    if (buffer_to_process == -1)
    {
        if (loader_state.file_is_open && loader_state.current_buffer_idx != -1 && !buffer_ready[loader_state.current_buffer_idx])
        {
            // Continue processing the currently open file/buffer
        }
        else if (loader_state.file_is_open)
        {
            if (loader_state.current_buffer_idx != -1 && buffer_ready[loader_state.current_buffer_idx])
            {
                f_close(&loader_state.current_file_handle);
                loader_state.file_is_open = false;
            }
            return;
        }
        else
        {
            return;
        }
    }
    else
    {
        if (!open_file_if_needed(buffer_to_process, target_frame_for_buffer[buffer_to_process]))
        {
            target_frame_for_buffer[buffer_to_process] = -1;
            if (buffer_to_process == 0 && !buffer_ready[1] && target_frame_for_buffer[1] != -1)
            {
                if (open_file_if_needed(1, target_frame_for_buffer[1]))
                { /* try to switch to buffer 1 */
                }
                else
                {
                    target_frame_for_buffer[1] = -1;
                    return;
                }
            }
            else if (buffer_to_process == 1 && !buffer_ready[0] && target_frame_for_buffer[0] != -1)
            {
                if (open_file_if_needed(0, target_frame_for_buffer[0]))
                { /* try to switch to buffer 0 */
                }
                else
                {
                    target_frame_for_buffer[0] = -1;
                    return;
                }
            }
            else
            {
                return;
            }
        }
    }

    if (!loader_state.file_is_open)
    {
        return;
    }

    UINT bytes_read_in_chunk = 0;
    size_t bytes_to_read_this_chunk = CHUNK_SIZE_BYTES;
    if (loader_state.current_file_offset + CHUNK_SIZE_BYTES > loader_state.current_file_total_size_bytes)
    {
        bytes_to_read_this_chunk = loader_state.current_file_total_size_bytes - loader_state.current_file_offset;
    }

    if (bytes_to_read_this_chunk > 0)
    {
        uint8_t *buffer_write_ptr = &frame_buffers[loader_state.current_buffer_idx][loader_state.current_file_offset];
        FRESULT fr = f_read(&loader_state.current_file_handle, buffer_write_ptr, bytes_to_read_this_chunk, &bytes_read_in_chunk);

        if (fr != FR_OK || (bytes_read_in_chunk == 0 && bytes_to_read_this_chunk > 0))
        {
            f_close(&loader_state.current_file_handle);
            loader_state.file_is_open = false;
            target_frame_for_buffer[loader_state.current_buffer_idx] = -1;
            buffer_ready[loader_state.current_buffer_idx] = false;
            return;
        }

        loader_state.current_file_offset += bytes_read_in_chunk;

        if (loader_state.current_file_offset >= loader_state.current_file_total_size_bytes)
        {
            buffer_ready[loader_state.current_buffer_idx] = true;
            // Keep file open for next frame
        }
    }
    else
    {
        if (loader_state.current_file_offset >= loader_state.current_file_total_size_bytes && !buffer_ready[loader_state.current_buffer_idx])
        {
            buffer_ready[loader_state.current_buffer_idx] = true;
            if (loader_state.file_is_open)
            {
                f_close(&loader_state.current_file_handle);
                loader_state.file_is_open = false;
            }
        }
    }
}

void sd_loader_mark_buffer_consumed(int buffer_idx, int next_target_frame)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return;

    // printf("SD_LOADER: Buffer %d consumed. New target frame: %d.\n", buffer_idx, next_target_frame);
    buffer_ready[buffer_idx] = false;
    int new_target = next_target_frame % loader_state.total_manifest_frames;
    target_frame_for_buffer[buffer_idx] = new_target;

    // If the file currently open was for the buffer we just consumed, it should be closed.
    // The next call to sd_loader_process will then open the new target file for this buffer (or another one).
    if (loader_state.file_is_open && loader_state.current_buffer_idx == buffer_idx)
    {
        // It's unlikely the file would still be open if the buffer was ready and consumed,
        // as sd_loader_process should have closed it upon full load.
        // But as a safeguard:
        // printf("SD_LOADER: Closing file for just-consumed buffer %d (was targeting frame %d).\n", buffer_idx, loader_state.current_frame_to_load_idx);
        // f_close(&loader_state.current_file_handle);
        // loader_state.file_is_open = false;
        // loader_state.current_buffer_idx = -1; // No buffer actively being loaded by this file op anymore.
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