#ifndef __PAV_UTIL
#define __PAV_UTIL

#include<time.h>
#include<assert.h>
#include<pipewire/pipewire.h>
#include<spa/param/audio/format-utils.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define NANOS_PER_SEC 1000000000

static float timespec_diff_ns(struct timespec *start, struct timespec *end) {
    time_t sec_diff = end->tv_sec - start->tv_sec;

    return sec_diff * NANOS_PER_SEC + (end->tv_nsec - start->tv_nsec);
}

typedef struct opts_s {
    int monitor;
    float sample_boost;
    int width;
    int height;
    int pw_source;

    bool log_timings;

    bool mirror;
    bool two_channels;
} opts_t;

typedef struct {
    float *samples;
    float *fft;
} channel_details_t;

typedef struct {
    struct pw_main_loop *loop;
    struct pw_stream *stream;

    struct spa_audio_info format;

    size_t n_total_samples;
    size_t n_samples;
    size_t n_channels;
    size_t relevant_fft_bins;
    channel_details_t *details;

    opts_t opts;

    struct timespec _last_render;
    struct timespec _last_audio_buffer;
} ctx_t;

Color color_progression(float progress) {
    progress = MAX(MIN(progress, 1), 0);
    assert(progress <= 1 && progress >= 0);

    Color color = CLITERAL(Color) { 0, 0, 0, 255 };


    if (progress <= 0.25) {
        //printf("b25: %f\n", progress);
        color.g = 255;
        color.b = (progress / 0.25) * 255;
    } else if (progress <= 0.50) {
        color.b = 255;
        color.g = 255 - (((progress - 0.25) / 0.25) * 255);
    } else if (progress <= 0.75) {
        color.b = 255;
        color.r = ((progress - 0.5) / 0.25) * 255;
    } else if (progress <= 1) {
        color.r = 255;
        color.b = 255 - (((progress - 0.75) / 0.25) * 255);
    }

    return color;
}

Color color_progression_alt(float progress) {
    progress = MAX(MIN(progress, 1), 0);
    assert(progress <= 1 && progress >= 0);

    Color color = (Color) { 0, 0, 0, 255 };

    if (progress <= 0.25) {
        color.g = 255;
        color.b = (progress / 0.25) * 255;
    } else if (progress <= 0.50) {
        color.b = 255;
        color.g = 255 - (((progress - 0.25) / 0.25) * 255);
    } else if (progress <= 0.75) {
        color.b = 255;
        color.r = ((progress - 0.5) / 0.25) * 255;
    } else if (progress <= 1) {
        color.r = 255;
        color.b = 255 - (((progress - 0.75) / 0.25) * 255);
    }

    return color;
}

#endif // __PAV_UTIL
