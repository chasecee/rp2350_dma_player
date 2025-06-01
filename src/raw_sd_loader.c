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
#define CHUNK_SIZE_SECTORS 256                            // Read 128 sectors (64KB) at once
#define BYTES_PER_SECTOR 512                              // Standard SD sector size

// Raw SD card parameters - use sector-aligned frame size for optimal performance
#define RAW_START_SECTOR 2048 // Start after partition table/boot sectors
#define PADDED_FRAME_SIZE_BYTES (((FRAME_SIZE_BYTES + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR) * BYTES_PER_SECTOR)
#define SECTORS_PER_FRAME (PADDED_FRAME_SIZE_BYTES / BYTES_PER_SECTOR)

// Define the actual storage for frame buffers and ready flags
__attribute__((aligned(32))) uint16_t frame_buffers[2][FRAME_HEIGHT * FRAME_WIDTH];
volatile bool buffer_ready[2] = {false, false};
volatile int target_frame_for_buffer[2] = {-1, -1};

// Temporary sector buffer for reading padded data
static uint8_t sector_buffer[CHUNK_SIZE_SECTORS * BYTES_PER_SECTOR] __attribute__((aligned(32)));

// Internal state for the raw loader
static struct
{
    int total_frames;
    int current_buffer_idx;
    int current_frame_to_load_idx;
    uint32_t current_sector_offset;
    uint32_t bytes_loaded_for_current_frame; // Track actual frame bytes, not sectors
} raw_loader_state;

// Track timing for adaptive loading
static uint32_t last_frame_load_time = 0;
static uint32_t avg_load_time = 0;
static uint32_t frame_count = 0;
static uint32_t read_start_time = 0; // Track when each read starts for timeout detection

void raw_sd_loader_init(int total_frames)
{
    DBG_PRINTF("RAW_SD_LOADER: Initializing with %d frames (raw sector access).\n", total_frames);
    DBG_PRINTF("RAW_SD_LOADER: Frame size: %d bytes, Padded: %d bytes, Sectors per frame: %d\n",
               FRAME_SIZE_BYTES, PADDED_FRAME_SIZE_BYTES, SECTORS_PER_FRAME);
    DBG_PRINTF("RAW_SD_LOADER: IMPORTANT - Only reading actual frame data (%d bytes), skipping %d padding bytes\n",
               FRAME_SIZE_BYTES, PADDED_FRAME_SIZE_BYTES - FRAME_SIZE_BYTES);
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
    raw_loader_state.bytes_loaded_for_current_frame = 0;

    DBG_PRINTF("RAW_SD_LOADER: Init complete. Frames start at sector %lu.\n", RAW_START_SECTOR);
}

static bool seek_and_prepare_raw_frame(int frame_idx_to_load)
{
    if (frame_idx_to_load < 0)
        frame_idx_to_load = 0;
    frame_idx_to_load = frame_idx_to_load % raw_loader_state.total_frames;

    // Calculate the starting sector for this frame
    uint32_t frame_start_sector = RAW_START_SECTOR + (frame_idx_to_load * SECTORS_PER_FRAME);

    // DON'T set current_buffer_idx here - let the caller manage it!
    // raw_loader_state.current_buffer_idx = 0;  // BUG: This was breaking dual-buffer loading!
    raw_loader_state.current_frame_to_load_idx = frame_idx_to_load;
    raw_loader_state.current_sector_offset = frame_start_sector;
    raw_loader_state.bytes_loaded_for_current_frame = 0;

    DBG_PRINTF("RAW_SD_LOADER: Seeking to frame %d at sector %lu\n", frame_idx_to_load, frame_start_sector);
    return true;
}

void raw_sd_loader_process(void)
{
    if (raw_loader_state.total_frames == 0)
        return;

    // CRITICAL: Check if display DMA is busy before doing ANYTHING
    // This prevents DMA conflict deadlocks between display and SD card
    extern volatile bool dma_transfer_complete;
    if (!dma_transfer_complete)
    {
        // Display DMA is busy - don't do any SD operations to avoid conflict
        return; // Try again next call when display DMA is done
    }

    // Check both buffers to see which one needs loading
    int buffer_to_load = -1;

    // If we're currently loading something, finish it first!
    if (raw_loader_state.current_buffer_idx >= 0)
    {
        // Continue with the current buffer until it's done
        buffer_to_load = raw_loader_state.current_buffer_idx;
    }
    else
    {
        // Not currently loading anything, pick a buffer that needs loading
        for (int i = 0; i < 2; i++)
        {
            if (!buffer_ready[i] && target_frame_for_buffer[i] >= 0)
            {
                buffer_to_load = i;
                break;
            }
        }
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
        DBG_PRINTF("RAW_SD_LOADER: Starting to load frame %d into buffer %d\n",
                   target_frame_for_buffer[buffer_to_load], buffer_to_load);
    }

    // Check if this frame is already fully loaded
    if (raw_loader_state.bytes_loaded_for_current_frame >= FRAME_SIZE_BYTES)
    {
        if (!buffer_ready[buffer_to_load])
            buffer_ready[buffer_to_load] = true;
        DBG_PRINTF("RAW_SD_LOADER: B%d ready with frame %d (%u bytes loaded)\n",
                   buffer_to_load, raw_loader_state.current_frame_to_load_idx,
                   raw_loader_state.bytes_loaded_for_current_frame);

        raw_loader_state.current_buffer_idx = -1;
        raw_loader_state.current_frame_to_load_idx = -1;
        return;
    }

    uint32_t start_time = to_us_since_boot(get_absolute_time());

    // Calculate how many bytes we still need for this frame
    uint32_t bytes_remaining = FRAME_SIZE_BYTES - raw_loader_state.bytes_loaded_for_current_frame;

    // Calculate how many sectors we need to read to get at least that many bytes
    uint32_t sectors_needed = (bytes_remaining + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
    uint32_t sectors_to_read = (sectors_needed > CHUNK_SIZE_SECTORS) ? CHUNK_SIZE_SECTORS : sectors_needed;

    // Debug: Show what we're about to read (only for first chunk of frame)
    if (raw_loader_state.bytes_loaded_for_current_frame == 0)
    {
        DBG_PRINTF("RAW_SD_LOADER: Starting B%d frame %d (need %u bytes, reading %u sectors from sector %lu)\n",
                   buffer_to_load, raw_loader_state.current_frame_to_load_idx,
                   FRAME_SIZE_BYTES, sectors_to_read, raw_loader_state.current_sector_offset);
    }

    // Read sectors into our temporary buffer first
    DBG_PRINTF("RAW_SD_LOADER: Reading %u sectors from %lu into temp buffer\n",
               sectors_to_read, raw_loader_state.current_sector_offset);

    // Record start time for timeout detection
    read_start_time = to_us_since_boot(get_absolute_time()) / 1000; // Convert to ms

    DRESULT dr = disk_read(0, sector_buffer, raw_loader_state.current_sector_offset, sectors_to_read);

    uint32_t read_end_time = to_us_since_boot(get_absolute_time()) / 1000;
    uint32_t read_duration = read_end_time - read_start_time;

    if (read_duration > 1000)
    { // More than 1 second is suspicious
        DBG_PRINTF("WARNING: SD read took %lu ms (sectors %lu-%lu)\n",
                   read_duration, raw_loader_state.current_sector_offset,
                   raw_loader_state.current_sector_offset + sectors_to_read - 1);
    }

    if (dr != RES_OK)
    {
        DBG_PRINTF("ERROR: RAW_SD_LOADER: Failed to read sectors (DR: %d) for B%d, frame %d.\n",
                   dr, buffer_to_load, raw_loader_state.current_frame_to_load_idx);
        DBG_PRINTF("ERROR: Details - sectors %lu-%lu, %u sectors\n",
                   raw_loader_state.current_sector_offset,
                   raw_loader_state.current_sector_offset + sectors_to_read - 1,
                   sectors_to_read);
        buffer_ready[buffer_to_load] = false;
        target_frame_for_buffer[buffer_to_load] = -2; // Indicate error/stale target
        raw_loader_state.current_buffer_idx = -1;
        raw_loader_state.current_frame_to_load_idx = -1;
        return;
    }

    // Now copy only the frame data we need from the sector buffer to the frame buffer
    uint32_t bytes_read = sectors_to_read * BYTES_PER_SECTOR;
    uint32_t bytes_to_copy = (bytes_remaining < bytes_read) ? bytes_remaining : bytes_read;

    uint8_t *frame_buffer_ptr = (uint8_t *)&frame_buffers[buffer_to_load][0] + raw_loader_state.bytes_loaded_for_current_frame;
    memcpy(frame_buffer_ptr, sector_buffer, bytes_to_copy);

    DBG_PRINTF("RAW_SD_LOADER: Copied %u bytes to frame buffer (now have %u/%u bytes for frame %d)\n",
               bytes_to_copy, raw_loader_state.bytes_loaded_for_current_frame + bytes_to_copy,
               FRAME_SIZE_BYTES, raw_loader_state.current_frame_to_load_idx);

    // Update our position
    raw_loader_state.current_sector_offset += sectors_to_read;
    raw_loader_state.bytes_loaded_for_current_frame += bytes_to_copy;

    // Check if frame is complete
    if (raw_loader_state.bytes_loaded_for_current_frame >= FRAME_SIZE_BYTES)
    {
        int successfully_loaded_frame = raw_loader_state.current_frame_to_load_idx;
        buffer_ready[buffer_to_load] = true;
        DBG_PRINTF("RAW_SD_LOADER: B%d ready with frame %d (%u bytes loaded)\n",
                   buffer_to_load, successfully_loaded_frame, raw_loader_state.bytes_loaded_for_current_frame);

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

    DBG_PRINTF("RAW_SD_LOADER: B%d consumed. Next target: %d\n", buffer_idx, new_target);
}

int raw_sd_loader_get_target_frame_for_buffer(int buffer_idx)
{
    if (buffer_idx < 0 || buffer_idx > 1)
        return -1;
    return target_frame_for_buffer[buffer_idx];
}