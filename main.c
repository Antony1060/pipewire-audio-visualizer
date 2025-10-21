#include<pipewire/pipewire.h>
#include<spa/param/audio/format-utils.h>
#include<stdio.h>
#include<stdint.h>
#include<signal.h>
#include<math.h>
#include<raylib.h>
#include<pthread.h>

#include "fft.c"
#include "spotify_dbus.c"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

// visual effect
#define MIRROR_FREQ 1

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

typedef struct ctx_s {
    struct pw_main_loop *loop;
    struct pw_stream *stream;

    struct spa_audio_info format;
    unsigned move: 1;

    float *samples;
    float *fft;
    size_t n_samples;
    size_t n_channels;

    opts_t opts;

#ifdef LOG_TIMINGS
    struct timespec _last_render;
    struct timespec _last_audio_buffer;
#endif // LOG_TIMINGS
} ctx_t;

void process_samples(ctx_t *ctx, float *samples, size_t n_samples) {
    for (size_t i = 0; i < n_samples; i++) {
        samples[i] *= ctx->opts.sample_boost;
    }
}

void fill_vector_from_samples(float *samples, size_t n_samples, Vector2 *coords, float centerline, int padding, float scale, float sample_chunk) {
    for (size_t i = 0; i < n_samples; i++) {
        coords[i].x = padding + sample_chunk * (i + 1);
        coords[i].y = centerline - (samples[i] * scale);
    }
}

void process_fft(float *fft, size_t n_samples) {
    for (size_t i = 0; i < n_samples; i++) {
        fft[i] = fabsf(fft[i]);
    }
}

void avg_reduce_stream(float *src, size_t src_size, float *dst, size_t dst_size, int max, int scale) {
    size_t chunk = src_size / dst_size;

    for (size_t i = 0; i < dst_size; i++) {
        float sum = 0;
        for (size_t j = i * chunk; j < (i + 1) * chunk; j++)
            sum += src[j];

        dst[i] = (sum / chunk) * scale;
    }

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
    const int SCALE = 300;

    const int REFRESH_RATE = GetMonitorRefreshRate(ctx->opts.monitor);
    SetTargetFPS(REFRESH_RATE);

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
            get_spotify_data(&spotify_data);
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

        float *fft = ctx->fft;

        Vector2 fft_coords[needed_fft_chunks];
        Vector2 coords[n_samples];

        int draw_width = S_WIDTH - PADDING * 2;

        fill_vector_from_samples(samples, n_samples, coords, S_HEIGHT / 2, PADDING, SCALE, (float) draw_width / n_samples);
        // peg first and last point to the edge, otherwise there will be a small gap at either side
        coords[0].x = PADDING;
        coords[n_samples - 1].x = S_WIDTH - PADDING;

        int freq_visible = MIN(256, needed_fft_chunks);
//        printf("needed %d\n", needed_fft_chunks);
        float fft_visible[freq_visible];
        process_fft(fft, n_samples);
        avg_reduce_stream(fft, needed_fft_chunks, fft_visible, freq_visible, 400, 8);

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

            float x_shift = i == freq_visible - 1 ? freq_draw_width + 0.1 : freq_draw_width;
            // weird rendering bug on first bar (I assume it's just floating point fuckery)
            DrawRectangle(point.x - x_shift, point.y, freq_draw_width, S_HEIGHT - point.y, BLUE);
        }

#if MIRROR_FREQ
        for (int i = 0; i < freq_visible; i++) {
            Vector2 point = fft_coords[i];

            DrawRectangle(S_WIDTH - point.x, point.y, freq_draw_width, S_HEIGHT - point.y, BLUE);
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

void on_state_changed(void *_ctx, enum pw_stream_state old, enum pw_stream_state new, const char *error) {
    (void) _ctx;

    if (error)
        printf("state changed: error: %s\n", error);

    printf("state changed: old: %s\n", pw_stream_state_as_string(old));
    printf("state changed: new: %s\n", pw_stream_state_as_string(new));
}

void on_param_changed(void *_ctx, uint32_t id, const struct spa_pod *param) {
    ctx_t *ctx = _ctx;

    if (param == NULL || id != SPA_PARAM_Format)
        return;

    if (spa_format_parse(param, &ctx->format.media_type, &ctx->format.media_subtype) < 0)
        return;

    if (ctx->format.media_type != SPA_MEDIA_TYPE_audio || ctx->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    spa_format_audio_raw_parse(param, &ctx->format.info.raw);

    printf("capturing rate: %d | channels: %d\n", ctx->format.info.raw.rate, ctx->format.info.raw.channels);
}

void on_process(void *_ctx) {
    ctx_t *ctx = _ctx;

    struct pw_buffer *b;
    if ((b = pw_stream_dequeue_buffer(ctx->stream)) == NULL) {
        pw_log_warn("out of buffers: %m");
        return;
    }

    struct spa_buffer *buf = b->buffer;

    float *samples;
    if ((samples = buf->datas[0].data) == NULL)
        return;

    struct timespec audio_start;
    clock_gettime(CLOCK_REALTIME, &audio_start);
    uint32_t n_samples = buf->datas[0].chunk->size / sizeof(float);
    uint32_t n_channels = ctx->format.info.raw.channels;

    if (ctx->n_samples != n_samples || ctx->n_channels != n_channels) {
        printf("samples: %d | channles: %d | samples_per_channel: %d\n", n_samples, n_channels, n_samples / n_channels);
    }

    ctx->samples = samples;
    ctx->n_samples = n_samples;
    ctx->n_channels = n_channels;
    process_samples(ctx, samples, n_samples);
    fft_samples(samples, ctx->fft, n_samples);

    pw_stream_queue_buffer(ctx->stream, b);

    struct timespec audio_end;
    clock_gettime(CLOCK_REALTIME, &audio_end);
#if LOG_TIMINGS
    float diff_ns = timespec_diff_ns(&audio_start, &audio_end);
    float last_diff_ms = timespec_diff_ns(&ctx->_last_audio_buffer, &audio_start) / 1000000;
    printf("audio sample cycle took %.2fns (%d/sec) (last was %.2fms ago)\n", diff_ns, (int) (NANOS_PER_SEC / diff_ns), last_diff_ms);
#endif
    ctx->_last_audio_buffer = audio_end;
}

struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
};

void do_quit(void *data, int signal) {
    (void) signal;

    pw_main_loop_quit(((ctx_t *) data)->loop);
}

void print_help(char *argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("    --help\n    \tdisplay this\n");
    printf("    --monitor/-m\n    \tint, usually 0-indexed, set the monitor to display the visualizer\n");
    printf("    --sample-boost/-sb\n    \tfloat, sometimes samples are too quiet to display nicely, this boosts them by some factor\n");
    printf("    --width/-w\n    \tint, default is monitor width\n");
    printf("    --height/-h\n    \tint, default is monitor height\n");
    printf("    --pw-source/-s\n    \tint, PipeWire node for source audio from, see --pw-source-list\n");
    printf("    --pw-source-lists\n    \tlist all PipeWire nodes\n");
}

void print_pw_sources() {
    printf("TODO\n");
}

int cli_parse(int argc, char **argv, opts_t *opts) {
    for (int i = 0; i < argc; i++) {
        char *arg = argv[i];
        
        if (!strcmp(arg, "--help")) {
            print_help(argv[0]);
            return 0;
        }
        
        if (!strcmp(arg, "--pw-source-list")) {
            print_pw_sources();
            return 0;
        }
        
        if ((!strcmp(arg, "--monitor") || !strcmp(arg, "-m")) && i + 1 < argc) {
            sscanf(argv[++i], "%d", &opts->monitor);
            continue;
        }
        
        if ((!strcmp(arg, "--sample-boost") || !strcmp(arg, "-sb")) && i + 1 < argc) {
            sscanf(argv[++i], "%f", &opts->sample_boost);
            continue;
        }
        
        if ((!strcmp(arg, "--width") || !strcmp(arg, "-w")) && i + 1 < argc) {
            sscanf(argv[++i], "%d", &opts->width);
            continue;
        }
        
        if ((!strcmp(arg, "--height") || !strcmp(arg, "-h")) && i + 1 < argc) {
            sscanf(argv[++i], "%d", &opts->height);
            continue;
        }
        
        if ((!strcmp(arg, "--pw-source") || !strcmp(arg, "-s")) && i + 1 < argc) {
            sscanf(argv[++i], "%d", &opts->pw_source);
            continue;
        }
    }

    return 1;
}

int main(int argc, char **argv) {
    float fft_buffer[10240];

    opts_t opts = {
        .monitor = 0,
        .sample_boost = 1,
        .width = 0,
        .height = 0,
        .pw_source = 0,
    };

    if (!cli_parse(argc, argv, &opts)) {
        return 0;
    }

    ctx_t ctx = {
        .fft = fft_buffer,
        .opts = opts
    };

    pthread_t tid;
    pthread_create(&tid, NULL, draw_thread_init, &ctx);

    pw_init(&argc, &argv);

    ctx.loop = pw_main_loop_new(NULL);

    pw_loop_add_signal(pw_main_loop_get_loop(ctx.loop), SIGINT, do_quit, &ctx);
    pw_loop_add_signal(pw_main_loop_get_loop(ctx.loop), SIGTERM, do_quit, &ctx);

    struct pw_properties *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Music",
            NULL);

    ctx.stream = pw_stream_new_simple(
            pw_main_loop_get_loop(ctx.loop),
            "idk-something",
            props,
            &stream_events,
            &ctx);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    const struct spa_pod *params[1] = {
        spa_format_audio_raw_build(
            &b,
            SPA_PARAM_EnumFormat,
            &SPA_AUDIO_INFO_RAW_INIT(
                .format = SPA_AUDIO_FORMAT_F32))
    };

    pw_stream_connect(ctx.stream,
            PW_DIRECTION_INPUT,
            ctx.opts.pw_source,
            //PW_ID_ANY,
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS,
            params, 1);

    pw_main_loop_run(ctx.loop);

    CloseWindow();

    pw_stream_destroy(ctx.stream);
    pw_main_loop_destroy(ctx.loop);
    pw_deinit();

    return 0;
}
