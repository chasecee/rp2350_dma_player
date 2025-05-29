#include "sd_loader.h"
#include "ff.h"     // For FatFs file operations
#include <stdio.h>  // For printf
#include <string.h> // For snprintf

// Define the actual storage for frame buffers and ready flags
uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];
volatile bool buffer_ready[2] = {false, false};

// Internal state for the loader
static const char (*g_frame_filenames)[MAX_FILENAME_LEN];
static int g_num_total_frames = 0;

// State for each loading buffer
static bool loading_in_progress[2] = {false, false};
static UINT bytes_loaded_current_attempt[2] = {0, 0};
static FIL file_object_for_load[2];
static int frame_target_for_buffer[2] = {-1, -1}; // Which frame index (from manifest) this buffer is trying to load

void sd_loader_init(const char (*frame_filenames_ptr)[MAX_FILENAME_LEN], int num_total_frames)
{
    g_frame_filenames = frame_filenames_ptr;
    g_num_total_frames = num_total_frames;

    // Initial targets: buffer 0 loads frame 0, buffer 1 loads frame 1
    // (or fewer if not enough frames)
    if (g_num_total_frames > 0)
    {
        frame_target_for_buffer[0] = 0;
    }
    if (g_num_total_frames > 1)
    {
        frame_target_for_buffer[1] = 1;
    }
    else if (g_num_total_frames > 0)
    {
        // If only one frame, buffer 1 targets it too, but will likely not be used much
        // or could be set to -1 if we want to be strict.
        // For now, let it target frame 0, sd_loader_process will handle not double-loading.
        frame_target_for_buffer[1] = 0;
    }
    printf("SD Loader initialized. %d total frames. Buffer 0 targets frame %d, Buffer 1 targets frame %d.\n",
           g_num_total_frames, frame_target_for_buffer[0], frame_target_for_buffer[1]);
}

void sd_loader_process(void)
{
    if (g_num_total_frames == 0)
        return;

    for (int i = 0; i < 2; ++i)
    { // Process for buffer 0, then buffer 1
        if (buffer_ready[i] || frame_target_for_buffer[i] == -1)
        {
            // This buffer is already full and ready for display, or has no valid target
            // OR another buffer is already loading this exact frame (advanced check not yet implemented here,
            // assuming main will manage not to request same frame for both buffers if not desired)
            continue;
        }

        if (!loading_in_progress[i])
        {
            // Not currently loading this buffer, and it's not ready. Try to start a new load.
            char current_frame_full_path[MAX_FILENAME_LEN + 8];
            snprintf(current_frame_full_path, sizeof(current_frame_full_path), "output/%s",
                     g_frame_filenames[frame_target_for_buffer[i]]);

            printf("SD Loader: Buffer %d, Prep open: [%s] (Target Frame %d)\n",
                   i, current_frame_full_path, frame_target_for_buffer[i]);
            fflush(stdout);

            FILINFO fno_stat_check;
            printf("SD Loader: Buffer %d, Attempt f_stat on: [%s]\n", i, current_frame_full_path);
            fflush(stdout);
            FRESULT fr_stat = f_stat(current_frame_full_path, &fno_stat_check);
            if (fr_stat == FR_OK)
            {
                printf("SD Loader: Buffer %d, f_stat OK for [%s], size: %lu, attr: 0x%02X\n", i, current_frame_full_path, fno_stat_check.fsize, fno_stat_check.fattrib);
            }
            else
            {
                printf("SD Loader: Buffer %d, f_stat FAILED for [%s] with error %d\n", i, current_frame_full_path, fr_stat);
            }
            fflush(stdout);

            printf("SD Loader: Buffer %d, BEFORE f_open for [%s]\n", i, current_frame_full_path);
            fflush(stdout);
            FRESULT fr = f_open(&file_object_for_load[i], current_frame_full_path, FA_READ);
            printf("SD Loader: Buffer %d, AFTER f_open for [%s], result: %d\n", i, current_frame_full_path, fr);
            fflush(stdout);

            if (fr == FR_OK)
            {
                printf("SD Loader: Buffer %d, Successfully opened [%s]\n", i, current_frame_full_path);
                fflush(stdout);
                loading_in_progress[i] = true;
                bytes_loaded_current_attempt[i] = 0;
                // First chunk will be read in the next block
            }
            else
            {
                printf("SD Loader: Buffer %d, ERROR Failed to open [%s]. FatFS error: %d\n",
                       i, current_frame_full_path, fr);
                fflush(stdout);
                frame_target_for_buffer[i] = -1;
                continue;
            }
        }

        // If we are here, loading_in_progress[i] must be true
        if (loading_in_progress[i])
        {
            UINT bytes_to_read_this_chunk = SD_READ_CHUNK_SIZE;
            UINT total_frame_size_bytes = FRAME_HEIGHT * FRAME_WIDTH * sizeof(uint16_t);
            UINT remaining_bytes = total_frame_size_bytes - bytes_loaded_current_attempt[i];

            if (bytes_to_read_this_chunk > remaining_bytes)
            {
                bytes_to_read_this_chunk = remaining_bytes;
            }

            if (bytes_to_read_this_chunk > 0)
            {
                uint8_t *buffer_offset_ptr = (uint8_t *)frame_buffers[i] + bytes_loaded_current_attempt[i];
                UINT bytes_actually_read;

                FRESULT fr = f_read(&file_object_for_load[i], buffer_offset_ptr, bytes_to_read_this_chunk, &bytes_actually_read);

                if (fr == FR_OK)
                {
                    bytes_loaded_current_attempt[i] += bytes_actually_read;

                    if (bytes_actually_read < bytes_to_read_this_chunk && bytes_loaded_current_attempt[i] < total_frame_size_bytes)
                    {
                        printf("SD Loader: WARN Read fewer bytes (%u) than requested (%u) for frame %d in buffer %d but not EOF. FatFS error may have occurred without FR_OK change. Total loaded: %u/%u\n",
                               bytes_actually_read, bytes_to_read_this_chunk, frame_target_for_buffer[i], i, bytes_loaded_current_attempt[i], total_frame_size_bytes);
                        f_close(&file_object_for_load[i]);
                        loading_in_progress[i] = false;
                        bytes_loaded_current_attempt[i] = 0;
                        buffer_ready[i] = false;
                        frame_target_for_buffer[i] = -1; // Problem with this frame target
                        continue;
                    }

                    if (bytes_loaded_current_attempt[i] >= total_frame_size_bytes)
                    {
                        // Frame fully loaded!
                        // printf("SD Loader: Frame %d successfully loaded into buffer %d (%u bytes).\n",
                        //        frame_target_for_buffer[i], i, bytes_loaded_current_attempt[i]);
                        f_close(&file_object_for_load[i]);
                        loading_in_progress[i] = false;
                        buffer_ready[i] = true;
                    }
                }
                else
                {
                    printf("SD Loader: ERROR f_read failed for frame %d in buffer %d. FatFS error: %d\n",
                           frame_target_for_buffer[i], i, fr);
                    f_close(&file_object_for_load[i]);
                    loading_in_progress[i] = false;
                    bytes_loaded_current_attempt[i] = 0;
                    buffer_ready[i] = false;
                    frame_target_for_buffer[i] = -1; // Problem with this frame target
                    continue;
                }
            }
            else
            { // bytes_to_read_this_chunk is 0, means frame should already be complete.
                if (bytes_loaded_current_attempt[i] >= total_frame_size_bytes && !buffer_ready[i])
                {
                    // This case should ideally be caught by the check above, but as a safeguard:
                    // printf("SD Loader: Frame %d in buffer %d seems complete based on byte count (safeguard), marking ready.\n",
                    //         frame_target_for_buffer[i], i);
                    if (loading_in_progress[i])
                        f_close(&file_object_for_load[i]); // Ensure closed if it was open
                    loading_in_progress[i] = false;
                    buffer_ready[i] = true;
                }
            }
        }
    } // end for each buffer
}

int sd_loader_get_target_frame_for_buffer(int buffer_idx)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return -1;
    return frame_target_for_buffer[buffer_idx];
}

void sd_loader_mark_buffer_consumed(int buffer_idx, int next_frame_to_target_for_this_buffer)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return;

    // printf("SD Loader: Buffer %d (was frame %d) consumed. New target: frame %d.\n",
    //        buffer_idx, frame_target_for_buffer[buffer_idx], next_frame_to_target_for_this_buffer);

    buffer_ready[buffer_idx] = false;
    loading_in_progress[buffer_idx] = false;
    bytes_loaded_current_attempt[buffer_idx] = 0;

    if (next_frame_to_target_for_this_buffer >= 0 && next_frame_to_target_for_this_buffer < g_num_total_frames)
    {
        // Basic check: don't let both buffers target the exact same *next* frame if the other buffer isn't ready with it yet
        // More sophisticated logic could be added if strict pipelining without re-targeting is needed.
        int other_buffer_idx = 1 - buffer_idx;
        if (frame_target_for_buffer[other_buffer_idx] == next_frame_to_target_for_this_buffer && !buffer_ready[other_buffer_idx] && loading_in_progress[other_buffer_idx])
        {
            printf("SD Loader: WARN: Buffer %d attempting to target frame %d, but buffer %d is already loading it. Adjusting target for %d.\n",
                   buffer_idx, next_frame_to_target_for_this_buffer, other_buffer_idx, buffer_idx);
            // Simple conflict avoidance: try to make it target one after the other buffer's target, wrapping around.
            // This is a basic strategy and might need refinement based on desired loading pattern.
            frame_target_for_buffer[buffer_idx] = (frame_target_for_buffer[other_buffer_idx] + 1) % g_num_total_frames;
        }
        else
        {
            frame_target_for_buffer[buffer_idx] = next_frame_to_target_for_this_buffer;
        }
    }
    else
    {
        // printf("SD Loader: Invalid next_frame_to_target %d for buffer %d. Setting to -1.\n",
        //        next_frame_to_target_for_this_buffer, buffer_idx);
        frame_target_for_buffer[buffer_idx] = -1; // Invalid target
    }
}