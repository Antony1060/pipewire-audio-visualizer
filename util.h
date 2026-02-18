#ifndef __PAV_UTIL
#define __PAV_UTIL
#include<time.h>
#include<pipewire/pipewire.h>
#include<spa/param/audio/format-utils.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define LOG_TIMINGS 0

#if LOG_TIMINGS
#define NANOS_PER_SEC 1000000000

static float timespec_diff_ns(struct timespec *start, struct timespec *end) {
    time_t sec_diff = end->tv_sec - start->tv_sec;

    return sec_diff * NANOS_PER_SEC + (end->tv_nsec - start->tv_nsec);
}
#endif

typedef struct opts_s {
    int monitor;
    float sample_boost;
    int width;
    int height;
    int pw_source;
} opts_t;

typedef struct {
    struct pw_main_loop *loop;
    struct pw_stream *stream;

    struct spa_audio_info format;

    float *samples;
    float *fft_magnitudes;
    size_t n_samples;
    size_t n_channels;

    opts_t opts;

#ifdef LOG_TIMINGS
    struct timespec _last_render;
    struct timespec _last_audio_buffer;
#endif // LOG_TIMINGS
} ctx_t;

#endif // __PAV_UTIL
