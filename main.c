#include<pipewire/pipewire.h>
#include<spa/param/audio/format-utils.h>
#include<stdio.h>
#include<stdint.h>
#include<signal.h>
#include<math.h>
#include<raylib.h>
#include<pthread.h>

#include "fft.c"

#define ENABLE_FREQ 1

typedef struct ctx_s {
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    
    struct spa_audio_info format;
    unsigned move: 1;

    float *samples;
    size_t n_samples;
    size_t n_channels;
} ctx_t;

void fill_vector_from_samples(float *samples, size_t n_samples, Vector2 *coords, float centerline, int padding, float scale, float sample_chunk) {
    for (size_t i = 0; i < n_samples; i++) {
        coords[i].x = padding + sample_chunk * i;
        coords[i].y = centerline - (samples[i] * scale);
    }
}

void *draw_thread_init(void *_ctx) {
    ctx_t *ctx = _ctx;

    const int S_WIDTH = 1920;
    const int S_HEIGHT = S_WIDTH / 16 * 9;

    const int PADDING = 20;
    const int SCALE = 300;

    InitWindow(S_WIDTH, S_HEIGHT, "audio visualizer");
    SetTargetFPS(165);

    (void) ctx;
    
    while(!WindowShouldClose()) {
        BeginDrawing();

        ClearBackground(BLACK);

        float *samples_all = ctx->samples;
        size_t n_samples_total = ctx->n_samples;
        size_t n_channels = ctx->n_channels;
        if (samples_all == NULL)
            continue;

        // TODO: merge channels
        size_t n_samples = n_samples_total / n_channels;

        float samples[n_samples];

        for (size_t i = 0; i < n_samples_total; i += n_channels) {
            samples[i / n_channels] = samples_all[i];
        }
        
        int needed_fft_chunks = (int) (20000.0 / ((double) ctx->format.info.raw.rate / n_samples));

        float fft[needed_fft_chunks];

#if ENABLE_FREQ
        fft_samples(samples, fft, n_samples, needed_fft_chunks);
#else
        memset(fft, 0, needed_fft_chunks * sizeof(float));
#endif

        Vector2 coords[n_samples];
        Vector2 fft_coords[needed_fft_chunks];

        int draw_width = S_WIDTH - PADDING * 2;

        fill_vector_from_samples(samples, n_samples, coords, S_HEIGHT / 2, PADDING, SCALE, (float) draw_width / n_samples);


        fill_vector_from_samples(fft, needed_fft_chunks, fft_coords, S_HEIGHT, 0, 10, (float) S_WIDTH / needed_fft_chunks);

        for (size_t i = 0; i < n_samples - 1; i++) {
            Vector2 start = coords[i];
            Vector2 end = coords[i + 1];

            DrawLineBezier(start, end, 2.0f, ORANGE);
        }

        for (int i = 0; i < needed_fft_chunks - 1; i++) {
            if (i < 1)
                continue;

            Vector2 start = fft_coords[i];
            Vector2 end = fft_coords[i + 1];

            DrawLineBezier(start, end, 2.0f, BLUE);
       }

        EndDrawing();
    }

    CloseWindow();

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
 
        uint32_t n_samples = buf->datas[0].chunk->size / sizeof(float);
        uint32_t n_channels = ctx->format.info.raw.channels;

        ctx->samples = samples;
        ctx->n_samples = n_samples;
        ctx->n_channels = n_channels;

        pw_stream_queue_buffer(ctx->stream, b);
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

int main(int argc, char **argv) {
    ctx_t ctx = {0};

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
            111,
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
