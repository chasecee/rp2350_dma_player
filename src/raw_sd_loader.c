#include "raw_sd_loader.h"
#include "diskio.h" // For direct disk_read access
#include <stdio.h>
#include <string.h>
#include "pico/time.h" // For get_absolute_time() and to_us_since_boot()
#include "hardware/timer.h"
#include "debug.h"

// Frame dimensions
#ifndef FRAME_WIDTH
#define FRAME_WIDTH 233
#endif
#ifndef FRAME_HEIGHT
#define FRAME_HEIGHT 233
#endif

#define FRAME_SIZE_BYTES (FRAME_HEIGHT * FRAME_WIDTH * 2) // Size of one frame in bytes
#define CHUNK_SIZE_SECTORS 128                            // Read 128 sectors (64KB) at once
#define BYTES_PER_SECTOR 512                              // Standard SD sector size

// Raw SD card parameters
#define RAW_START_SECTOR 2048 // Start after partition table/boot sectors
#define SECTORS_PER_FRAME ((FRAME_SIZE_BYTES + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR)

// Define the actual storage for frame buffers and ready flags
__attribute__((aligned(32))) uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];
volatile bool buffer_ready[2] = {false, false};
volatile int target_frame_for_buffer[2] = {-1, -1};

// Internal state for the raw loader
static struct
{
    int total_frames;
    int current_buffer_idx;
    int current_frame_to_load_idx;
    uint32_t current_sector_offset;
    uint32_t sectors_loaded_for_current_frame;
} raw_loader_state;

// Track timing for adaptive loading
static uint32_t last_frame_load_time = 0;
static uint32_t avg_load_time = 0;
static uint32_t frame_count = 0;

void raw_sd_loader_init(int total_frames)
{
    DBG_PRINTF("RAW_SD_LOADER: Initializing with %d frames (raw sector access).\n", total_frames);
    raw_loader_state.total_frames = total_frames;
    if (total_frames == 0)
    {
        DBG_PRINTF("RAW_SD_LOADER: No frames to load.\n");
        return;
    }
    buffer_ready[0] = false;
    buffer_ready[1] = false;
    target_frame_for_buffer[0] = 0; // Buffer 0 loads frame 0
    target_frame_for_buffer[1] = 1; // Buffer 1 loads frame 1
    raw_loader_state.current_buffer_idx = -1;
    raw_loader_state.current_frame_to_load_idx = -1;
    raw_loader_state.current_sector_offset = 0;
    raw_loader_state.sectors_loaded_for_current_frame = 0;

    DBG_PRINTF("RAW_SD_LOADER: Init complete. Frames start at sector %lu.\n", RAW_START_SECTOR);
}

static bool seek_and_prepare_raw_frame(int frame_idx_to_load)
{
    if (frame_idx_to_load < 0)
        frame_idx_to_load = 0;
    frame_idx_to_load = frame_idx_to_load % raw_loader_state.total_frames;

    // Calculate the starting sector for this frame
    uint32_t frame_start_sector = RAW_START_SECTOR + (frame_idx_to_load * SECTORS_PER_FRAME);

    raw_loader_state.current_buffer_idx = 0;
    raw_loader_state.current_frame_to_load_idx = frame_idx_to_load;
    raw_loader_state.current_sector_offset = frame_start_sector;
    raw_loader_state.sectors_loaded_for_current_frame = 0;

    DBG_PRINTF("RAW_SD_LOADER: Seeking to frame %d at sector %lu\n", frame_idx_to_load, frame_start_sector);
    return true;
}

void raw_sd_loader_process(void)
{
    if (raw_loader_state.total_frames == 0)
        return;

    // Check both buffers to see which one needs loading
    int buffer_to_load = -1;

    // If we're not currently loading anything, pick a buffer to load
    if (raw_loader_state.current_buffer_idx == -1)
    {
        for (int i = 0; i < 2; i++)
        {
            if (!buffer_ready[i] && target_frame_for_buffer[i] >= 0)
            {
                buffer_to_load = i;
                break;
            }
        }
    }
    else
    {
        // Continue loading the current buffer
        buffer_to_load = raw_loader_state.current_buffer_idx;
    }

    if (buffer_to_load == -1)
    {
        // No buffer needs loading right now
        return;
    }

    // If we're starting a new load, seek to the frame
    if (raw_loader_state.current_buffer_idx != buffer_to_load ||
        raw_loader_state.current_frame_to_load_idx != target_frame_for_buffer[buffer_to_load])
    {
        if (!seek_and_prepare_raw_frame(target_frame_for_buffer[buffer_to_load]))
        {
            return;
        }
        raw_loader_state.current_buffer_idx = buffer_to_load;
    }

    // Check if this frame is already fully loaded
    if (raw_loader_state.sectors_loaded_for_current_frame >= SECTORS_PER_FRAME)
    {
        if (!buffer_ready[buffer_to_load])
            buffer_ready[buffer_to_load] = true;
        raw_loader_state.current_buffer_idx = -1; // Reset to allow re-evaluation next call
        raw_loader_state.current_frame_to_load_idx = -1;
        return;
    }

    uint32_t start_time = to_us_since_boot(get_absolute_time());

    // Calculate how many sectors we still need to read
    uint32_t sectors_remaining = SECTORS_PER_FRAME - raw_loader_state.sectors_loaded_for_current_frame;
    uint32_t sectors_to_read = (sectors_remaining > CHUNK_SIZE_SECTORS) ? CHUNK_SIZE_SECTORS : sectors_remaining;

    // Calculate where to write in the buffer (byte offset)
    uint32_t byte_offset_in_frame = raw_loader_state.sectors_loaded_for_current_frame * BYTES_PER_SECTOR;
    uint8_t *buffer_write_ptr = (uint8_t *)&frame_buffers[buffer_to_load][0] + byte_offset_in_frame;

    // Debug: Show what we're about to read (only for first chunk of frame)
    if (raw_loader_state.sectors_loaded_for_current_frame == 0)
    {
        DBG_PRINTF("RAW_SD_LOADER: Starting B%d frame %d (%u sectors from sector %lu)\n",
                   buffer_to_load, raw_loader_state.current_frame_to_load_idx,
                   SECTORS_PER_FRAME, raw_loader_state.current_sector_offset);
    }

    // Direct sector read - bypasses FatFS entirely!
    DRESULT dr = disk_read(0, buffer_write_ptr, raw_loader_state.current_sector_offset, sectors_to_read);

    if (dr != RES_OK)
    {
        DBG_PRINTF("ERROR: RAW_SD_LOADER: Failed to read sectors (DR: %d) for B%d, frame %d.\n",
                   dr, buffer_to_load, raw_loader_state.current_frame_to_load_idx);
        buffer_ready[buffer_to_load] = false;
        target_frame_for_buffer[buffer_to_load] = -2; // Indicate error/stale target
        raw_loader_state.current_buffer_idx = -1;
        raw_loader_state.current_frame_to_load_idx = -1;
        return;
    }

    // Update our position
    raw_loader_state.current_sector_offset += sectors_to_read;
    raw_loader_state.sectors_loaded_for_current_frame += sectors_to_read;

    // Check if frame is complete
    if (raw_loader_state.sectors_loaded_for_current_frame >= SECTORS_PER_FRAME)
    {
        int successfully_loaded_frame = raw_loader_state.current_frame_to_load_idx;
        buffer_ready[buffer_to_load] = true;

        raw_loader_state.current_buffer_idx = -1;
        raw_loader_state.current_frame_to_load_idx = -1;

        uint32_t end_time = to_us_since_boot(get_absolute_time());
        uint32_t load_time = (end_time - start_time) / 1000; // Convert to milliseconds
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
        DBG_PRINTF("RAW_SD_LOADER: B%d loaded frame %d took %lu ms. Avg: %lu ms.\n",
                   buffer_to_load, successfully_loaded_frame, load_time, avg_load_time);
    }
}

void raw_sd_loader_mark_buffer_consumed(int buffer_idx, int next_target_frame)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return; // Invalid buffer index

    buffer_ready[buffer_idx] = false;
    int new_target = next_target_frame % raw_loader_state.total_frames;
    target_frame_for_buffer[buffer_idx] = new_target;

    // DBG_PRINTF("RAW_SD_LOADER: B%d consumed. Next target: %d\n", buffer_idx, new_target);
}

int raw_sd_loader_get_target_frame_for_buffer(int buffer_idx)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return -1;
    return target_frame_for_buffer[buffer_idx];
}