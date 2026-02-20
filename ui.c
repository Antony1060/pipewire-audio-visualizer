#include<stdio.h>
#include<stdint.h>
#include<stddef.h>
#include<math.h>
#include<time.h>
#include<raylib.h>
#include<assert.h>

#include "util.h"
#include "spotify_dbus.c"

#define COLOR_PROGRESSION(ctx) (((ctx)->opts.flip_colors) ? color_progression_alt : color_progression)
#define COLOR_PROGRESSION_ALT(ctx) (((ctx)->opts.flip_colors) ? color_progression : color_progression_alt)

int S_WIDTH = -1;
int S_HEIGHT = -1;

void avg_reduce_stream(float *src, size_t src_size, float *dst, size_t dst_size, float scale) {
    // reduce src chunks to dst chunks
    size_t chunk = src_size / dst_size;

    for (size_t i = 0; i < dst_size; i++) {
        float sum = 0;
        for (size_t j = i * chunk; j < (i + 1) * chunk; j++)
            sum += src[j];

        dst[i] = (sum / chunk) * scale;
    }
}

void merge_channels(channel_details_t *all_details, channel_details_t *dst, size_t n_samples, size_t n_channels) {
    for (size_t i = 0; i < n_samples; i++) {
        float sum = 0;
        float sum_fft = 0;

        for (size_t j = 0; j < n_channels; j++) {
           sum += all_details[j].samples[i];
           sum_fft += all_details[j].fft[i];
        }

        dst->samples[i] = sum / n_channels;
        dst->fft[i] = sum_fft / n_channels;
    }
}

void fill_vector_from_samples(float *samples, size_t n_samples, Vector2 *coords, float centerline, int padding, float scale, float sample_chunk) {
    for (size_t i = 0; i < n_samples; i++) {
        coords[i].x = padding + sample_chunk * (i + 1);
        coords[i].y = centerline - (samples[i] * scale);
    }
}

void render_samples(float *samples, size_t n_samples, float centerline, Color (*color_progression_fn)(float)) {
    const int PADDING = 0;
    const int SCALE = 40;

    float sample_max = 0;
    for (size_t i = 0; i < n_samples; i++)
        sample_max = MAX(sample_max, fabsf(samples[i]));

    Vector2 coords[n_samples];

    int draw_width = S_WIDTH - PADDING * 2;

    fill_vector_from_samples(samples, n_samples, coords, centerline, PADDING, SCALE, (float) draw_width / n_samples);

    // peg first and last point to the edge, otherwise there will be a small gap at either side
    coords[0].x = PADDING;
    coords[n_samples - 1].x = S_WIDTH - PADDING;

    for (size_t i = 0; i < n_samples - 1; i += 2) {
        Vector2 start = coords[i];
        Vector2 end = coords[i + 1];

        Color color = color_progression_fn(fabsf(samples[i + 1]) / sample_max);
        DrawLineBezier(start, end, 2.0f, color);

        if (i + 2 < n_samples) {
            Vector2 start2 = coords[i + 2];
            DrawLineBezier(end, start2, 2.0f, color);
        }
    }
}

void prepare_fft_render(ctx_t *ctx, Vector2 *dst, float *magnitudes) {
    size_t freq_visible = MIN(256, ctx->relevant_fft_bins);
    float fft_visible[freq_visible];

    avg_reduce_stream(magnitudes, ctx->relevant_fft_bins, fft_visible, freq_visible, 0.4);

    fill_vector_from_samples(fft_visible, freq_visible, dst, S_HEIGHT - 1, 0, 1, (float) S_WIDTH / freq_visible);
}

void render_mono_channel(ctx_t *ctx) {
    float samples[ctx->n_samples];
    float fft[ctx->n_samples];
    channel_details_t _curr = { .samples = samples, .fft = fft };

    merge_channels(ctx->details, &_curr, ctx->n_samples, ctx->n_channels);

    render_samples(samples, ctx->n_samples, S_HEIGHT / 2, COLOR_PROGRESSION(ctx));

    // rendering fft
    Vector2 fft_coords[ctx->relevant_fft_bins];
    prepare_fft_render(ctx, fft_coords, fft);

    size_t freq_visible = MIN(256, ctx->relevant_fft_bins);
    float freq_draw_width = (float) (S_WIDTH / freq_visible);

    for (size_t i = 0; i < freq_visible; i++) {
        Vector2 point = fft_coords[i];

        // weird rendering bug on first bar (I assume it's just floating point fuckery)
        float x_shift = i == freq_visible - 1 ? freq_draw_width + 0.1 : freq_draw_width;

        Vector2 pos = { point.x - x_shift, point.y };
        Vector2 size = { freq_draw_width, S_HEIGHT - point.y };
        DrawRectangleV(pos, size, BLUE);
    }

    if (ctx->opts.mirror) {
        for (size_t i = 0; i < freq_visible; i++) {
            Vector2 point = fft_coords[i];

            Vector2 pos = { S_WIDTH - point.x, point.y };
            Vector2 size = { freq_draw_width, S_HEIGHT - point.y };
            DrawRectangleV(pos, size, BLUE);
        }
    }
}


void render_two_channels(ctx_t *ctx) {
    assert(ctx->n_channels == 2);

    size_t centerline_offset = ctx->opts.split_waves ? 200 : 0;
    render_samples(ctx->details[0].samples, ctx->n_samples, S_HEIGHT / 2 - centerline_offset, COLOR_PROGRESSION(ctx));
    render_samples(ctx->details[1].samples, ctx->n_samples, S_HEIGHT / 2 + centerline_offset, COLOR_PROGRESSION_ALT(ctx));

    // rendering fft
    Vector2 fft_coords_r[ctx->relevant_fft_bins];
    Vector2 fft_coords_l[ctx->relevant_fft_bins];

    prepare_fft_render(ctx, fft_coords_r, ctx->details[0].fft);
    prepare_fft_render(ctx, fft_coords_l, ctx->details[1].fft);

    size_t freq_visible = MIN(256, ctx->relevant_fft_bins);
    float freq_draw_width = (float) (S_WIDTH / freq_visible);

    for (size_t i = 0; i < freq_visible; i++) {
        Vector2 point = fft_coords_r[i];

        // weird rendering bug on first bar (I assume it's just floating point fuckery)
        float x_shift = i == freq_visible - 1 ? freq_draw_width + 0.1 : freq_draw_width;

        Vector2 pos = { point.x - x_shift, point.y };
        Vector2 size = { freq_draw_width, S_HEIGHT - point.y };
        DrawRectangleV(pos, size, BLUE);
    }

    for (size_t i = 0; i < freq_visible; i++) {
        Vector2 point = fft_coords_l[i];

        Vector2 pos = { S_WIDTH - point.x, point.y };
        Vector2 size = { freq_draw_width, S_HEIGHT - point.y };
        DrawRectangleV(pos, size, BLUE);
    }
}

void *draw_thread_init(void *_ctx) {
    ctx_t *ctx = _ctx;

    SetConfigFlags(FLAG_WINDOW_TRANSPARENT | FLAG_WINDOW_UNDECORATED);

    const int REFRESH_RATE = GetMonitorRefreshRate(ctx->opts.monitor);
    SetTargetFPS(REFRESH_RATE);

    InitWindow(0, 0, "audio visualizer");

    S_WIDTH = ctx->opts.width == 0 ? GetMonitorWidth(ctx->opts.monitor) : ctx->opts.width;
    S_HEIGHT = ctx->opts.height == 0 ? GetMonitorHeight(ctx->opts.monitor) : ctx->opts.height;
    SetWindowMonitor(ctx->opts.monitor);

    SetWindowSize(S_WIDTH, S_HEIGHT);

    const time_t SPOTIFY_FETCH_INTERVAL = 1;
    spotify_data_t spotify_data = {0};
    time_t spotify_last_get = 0;

    Font font;
    if (ctx->opts.font != NULL) {
        // load Latin Extended-A
        const size_t count =  0x017F - 0x0020 + 1;
        int codepoints[count];
        for (size_t i = 0; i < count; i++)
            codepoints[i] = 0x0020 + i;

        font = LoadFontEx(ctx->opts.font, 128, codepoints, count);
    }

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
            if (ctx->opts.font != NULL && font.texture.id != 0) {
                DrawTextEx(font, spotify_data.artist, (Vector2) { 100, 100 }, 36, 0, WHITE);
                DrawTextEx(font, spotify_data.title, (Vector2) { 100, 140 }, 72, 0, WHITE);
            } else {
                DrawText(spotify_data.artist, 100, 100, 32, WHITE);
                DrawText(spotify_data.title, 100, 140, 64, WHITE);
            }
        }

        if (ctx->details == NULL)
            continue;

        if (ctx->opts.two_channels)
            render_two_channels(ctx);
        else
            render_mono_channel(ctx);


        struct timespec render_end;
        clock_gettime(CLOCK_REALTIME, &render_end);

        if (ctx->opts.log_timings) {
            float diff_ms = timespec_diff_ns(&render_start, &render_end) / 1000000;
            float last_diff_ms = timespec_diff_ns(&ctx->_last_render, &render_start) / 1000000;
            fprintf(stderr, "render took %.2fms (%d/sec) (last was %.2fms ago)\n", diff_ms, (int) (1000 / diff_ms), last_diff_ms);
        }

        ctx->_last_render = render_end;

        EndDrawing();
    }

    CloseWindow();

    pw_main_loop_quit(ctx->loop);

    return NULL;
}
