#include<stdio.h>
#include<stdint.h>
#include<stddef.h>
#include<math.h>
#include<time.h>
#include<raylib.h>
#include<assert.h>

#include "util.h"
#include "spotify_dbus.c"

// visual effect
#define MIRROR_FREQ 1

void avg_reduce_stream(float *src, size_t src_size, float *dst, size_t dst_size, int max) {
    // reduce src chunks to dst chunks
    size_t chunk = src_size / dst_size;

    for (size_t i = 0; i < dst_size; i++) {
        float sum = 0;
        for (size_t j = i * chunk; j < (i + 1) * chunk; j++)
            sum += src[j];

        dst[i] = (sum / chunk) * 0.2;
    }

    // scale to max
    float sample_max = 0;
    for (size_t i = 0; i < dst_size; i++)
        sample_max = MAX(sample_max, dst[i]);

    max = MIN(sample_max, max);

    for (size_t i = 0; i < dst_size; i++)
        dst[i] = (dst[i] / sample_max) * max;
}

void merge_sample_channels(float *samples, float *dst, size_t n_samples, size_t n_channels) {
    size_t dst_size = n_samples / n_channels;

    for (size_t i = 0; i < dst_size; i++) {
        float sum = 0;
        for (size_t j = i * n_channels; j < (i + 1) * n_channels; j++)
           sum += samples[j];

        dst[i] = sum / n_channels;
    }
}

void fill_vector_from_samples(float *samples, size_t n_samples, Vector2 *coords, float centerline, int padding, float scale, float sample_chunk) {
    for (size_t i = 0; i < n_samples; i++) {
        coords[i].x = padding + sample_chunk * (i + 1);
        coords[i].y = centerline - (samples[i] * scale);
    }
}

Color color_progression(float progress) {
    progress = MAX(MIN(progress, 1), 0);
    assert(progress <= 1 && progress >= 0);

    Color color = CLITERAL(Color) { 0, 0, 0, 255 };

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

void *draw_thread_init(void *_ctx) {
    ctx_t *ctx = _ctx;

    SetConfigFlags(FLAG_WINDOW_TRANSPARENT | FLAG_WINDOW_UNDECORATED);

    const int PADDING = 0;
    const int SCALE = 40;

    //const int REFRESH_RATE = GetMonitorRefreshRate(ctx->opts.monitor);
    SetTargetFPS(60);

    InitWindow(0, 0, "audio visualizer");

    const int S_WIDTH = ctx->opts.width == 0 ? GetMonitorWidth(ctx->opts.monitor) : ctx->opts.width;
    const int S_HEIGHT = ctx->opts.height == 0 ? GetMonitorHeight(ctx->opts.monitor) : ctx->opts.height;
    SetWindowMonitor(ctx->opts.monitor);

    SetWindowSize(S_WIDTH, S_HEIGHT);

    const time_t SPOTIFY_FETCH_INTERVAL = 1;
    spotify_data_t spotify_data = {0};
    time_t spotify_last_get = 0;

    while(!WindowShouldClose()) {
        struct timespec render_start;
        clock_gettime(CLOCK_REALTIME, &render_start);

        char title[64];
        sprintf(title, "audio visualizer | fps: %d", GetFPS());
        SetWindowTitle(title);

        BeginDrawing();

        ClearBackground(BLANK);

        time_t now = time(NULL);
        if (now - spotify_last_get >= SPOTIFY_FETCH_INTERVAL) {
            if (get_spotify_data(&spotify_data) < 0) {
                spotify_data = (spotify_data_t) {0};
            }
            spotify_last_get = now;
        }

        if (spotify_data.artist != 0) {
            DrawText(spotify_data.artist, 100, 100, 32, WHITE);
            DrawText(spotify_data.title, 100, 140, 64, WHITE);
        }

        float *samples_all = ctx->samples;
        size_t n_samples_total = ctx->n_samples;
        size_t n_channels = ctx->n_channels;
        if (samples_all == NULL)
            continue;

        size_t n_samples = n_samples_total / n_channels;

        float samples[n_samples];

        merge_sample_channels(samples_all, samples, n_samples_total, n_channels);

        float sample_max = 0;
        for (size_t i = 0; i < n_samples; i++)
            sample_max = MAX(sample_max, fabsf(samples[i]));

        int needed_fft_chunks = (int) (20000.0 / ((double) ctx->format.info.raw.rate / n_samples));

        float *fft = ctx->fft_magnitudes;

        Vector2 fft_coords[needed_fft_chunks];
        Vector2 coords[n_samples];

        int draw_width = S_WIDTH - PADDING * 2;

        fill_vector_from_samples(samples, n_samples, coords, S_HEIGHT / 2, PADDING, SCALE, (float) draw_width / n_samples);
        // peg first and last point to the edge, otherwise there will be a small gap at either side
        coords[0].x = PADDING;
        coords[n_samples - 1].x = S_WIDTH - PADDING;

        int freq_visible = MIN(256, needed_fft_chunks);
        float fft_visible[freq_visible];
        avg_reduce_stream(fft, needed_fft_chunks, fft_visible, freq_visible, 400);

        fill_vector_from_samples(fft_visible, freq_visible, fft_coords, S_HEIGHT - 1, 0, 1, (float) S_WIDTH / freq_visible);

        for (size_t i = 0; i < n_samples - 1; i += 2) {
            Vector2 start = coords[i];
            Vector2 end = coords[i + 1];

            Color color = color_progression(fabsf(1 - (fabsf(samples[i + 1]) / sample_max / 2)));
            DrawLineBezier(start, end, 2.0f, color);

            if (i + 2 < n_samples) {
                Vector2 start2 = coords[i + 2];
                DrawLineBezier(end, start2, 2.0f, color);
            }
        }

        float freq_draw_width = (float) (S_WIDTH / freq_visible);
        for (int i = 0; i < freq_visible; i++) {
            Vector2 point = fft_coords[i];

            // weird rendering bug on first bar (I assume it's just floating point fuckery)
            float x_shift = i == freq_visible - 1 ? freq_draw_width + 0.1 : freq_draw_width;

            Vector2 pos = { point.x - x_shift, point.y };
            Vector2 size = { freq_draw_width, S_HEIGHT - point.y };
            DrawRectangleV(pos, size, BLUE);
        }

#if MIRROR_FREQ
        for (int i = 0; i < freq_visible; i++) {
            Vector2 point = fft_coords[i];

            Vector2 pos = { S_WIDTH - point.x, point.y };
            Vector2 size = { freq_draw_width, S_HEIGHT - point.y };
            DrawRectangleV(pos, size, BLUE);
        }
#endif

        struct timespec render_end;
        clock_gettime(CLOCK_REALTIME, &render_end);
#if LOG_TIMINGS
        float diff_ms = timespec_diff_ns(&render_start, &render_end) / 1000000;
        float last_diff_ms = timespec_diff_ns(&ctx->_last_render, &render_start) / 1000000;
        printf("render took %.2fms (%d/sec) (last was %.2fms ago)\n", diff_ms, (int) (1000 / diff_ms), last_diff_ms);
#endif
        ctx->_last_render = render_end;

        EndDrawing();
    }

    CloseWindow();

    pw_main_loop_quit(ctx->loop);

    return NULL;
}
