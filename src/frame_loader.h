#ifndef FRAME_LOADER_H
#define FRAME_LOADER_H

#include <stdint.h>
#include <stdbool.h>

// Frame loader configuration
typedef struct {
    int total_frames;
    int frame_width;
    int frame_height;
} frame_loader_config_t;

// Frame loader interface
bool frame_loader_init(const frame_loader_config_t *config);
void frame_loader_process(void);

// Frame buffer management
bool frame_loader_has_frame(int frame_index);
const uint16_t* frame_loader_get_frame(int frame_index);
void frame_loader_mark_frame_consumed(int frame_index, int next_frame_to_load);

// Status and debug
int frame_loader_get_buffer_for_frame(int frame_index);
bool frame_loader_is_loading(void);

#endif // FRAME_LOADER_H 