#include "frame_loader.h"
#include "raw_sd_loader.h"
#include "debug.h"
#include <stddef.h>

// Internal state
static frame_loader_config_t config;
static bool initialized = false;

bool frame_loader_init(const frame_loader_config_t *cfg)
{
    if (!cfg || cfg->total_frames <= 0)
    {
        return false;
    }

    config = *cfg;

    DBG_PRINTF("FRAME_LOADER: Initializing with %d frames (%dx%d)\n",
               config.total_frames, config.frame_width, config.frame_height);

    // Initialize the underlying raw SD loader
    raw_sd_loader_init(config.total_frames);

    initialized = true;
    return true;
}

void frame_loader_process(void)
{
    if (!initialized)
    {
        return;
    }

    // Delegate to the raw SD loader
    raw_sd_loader_process();
}

bool frame_loader_has_frame(int frame_index)
{
    if (!initialized)
    {
        return false;
    }

    // Check both buffers to see if either has our frame
    for (int i = 0; i < 2; i++)
    {
        if (buffer_ready[i] && raw_sd_loader_get_target_frame_for_buffer(i) == frame_index)
        {
            return true;
        }
    }

    return false;
}

const uint16_t *frame_loader_get_frame(int frame_index)
{
    if (!initialized)
    {
        return NULL;
    }

    // Find which buffer has our frame
    for (int i = 0; i < 2; i++)
    {
        if (buffer_ready[i] && raw_sd_loader_get_target_frame_for_buffer(i) == frame_index)
        {
            return &frame_buffers[i][0];
        }
    }

    return NULL;
}

void frame_loader_mark_frame_consumed(int frame_index, int next_frame_to_load)
{
    if (!initialized)
    {
        return;
    }

    // Find which buffer has the consumed frame and mark it for next loading
    for (int i = 0; i < 2; i++)
    {
        if (buffer_ready[i] && raw_sd_loader_get_target_frame_for_buffer(i) == frame_index)
        {
            raw_sd_loader_mark_buffer_consumed(i, next_frame_to_load);
            DBG_PRINTF("FRAME_LOADER: Frame %d consumed from buffer %d, next target: %d\n",
                       frame_index, i, next_frame_to_load);
            return;
        }
    }

    DBG_PRINTF("FRAME_LOADER: Warning - couldn't find buffer for frame %d to consume\n", frame_index);
}

int frame_loader_get_buffer_for_frame(int frame_index)
{
    if (!initialized)
    {
        return -1;
    }

    // Find which buffer has our frame
    for (int i = 0; i < 2; i++)
    {
        if (buffer_ready[i] && raw_sd_loader_get_target_frame_for_buffer(i) == frame_index)
        {
            return i;
        }
    }

    return -1;
}

bool frame_loader_is_loading(void)
{
    if (!initialized)
    {
        return false;
    }

    // Check if any buffer is currently being loaded
    for (int i = 0; i < 2; i++)
    {
        if (!buffer_ready[i] && target_frame_for_buffer[i] >= 0)
        {
            return true;
        }
    }

    return false;
}