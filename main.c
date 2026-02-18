#include<pipewire/pipewire.h>
#include<spa/param/audio/format-utils.h>
#include<stdio.h>
#include<stdint.h>
#include<signal.h>
#include<math.h>
#include<raylib.h>
#include<pthread.h>

#include "util.h"
#include "fft.c"
#include "pipewire_enumerate.c"
#include "ui.c"

#define FFT_BUF_SIZE 10240

// very crude normalization
void normalize_samples(float *samples, size_t n_samples) {
    const double RMS_TARGET = 1.6;
    const int RMS_BUF_SIZE = 64 * 1024;
    
    static double rms_buffer[64 * 1024];
    static size_t cursor = 0;

    for (size_t i = 0; i < n_samples; i++) {
        rms_buffer[cursor] = samples[i];
        cursor = (cursor + 1) & (RMS_BUF_SIZE - 1);
    }

    double total = 0;
    for (size_t i = 0; i < RMS_BUF_SIZE; i++) {
        total += rms_buffer[i] * rms_buffer[i];
    }
    
    double avg = sqrt(total / RMS_BUF_SIZE);
    double gain = RMS_TARGET / avg;

    for (size_t i = 0; i < n_samples; i++) {
        samples[i] = samples[i] * gain;
    }

    //printf("rms: %f | s_rms: %f | gain: %f\n", avg, sqrt(t2 / n_samples), gain);
}

void process_samples(ctx_t *ctx, float *samples, size_t n_samples) {
    for (size_t i = 0; i < n_samples; i++) {
        samples[i] *= ctx->opts.sample_boost;
    }

    //normalize_samples(samples, n_samples);
}

void process_fft(float *magnitudes, float *fft_real, float *fft_imag, size_t n_samples) {
    float total = 0;
    for (size_t i = 0; i < n_samples; i++) {
        magnitudes[i] = sqrt(fft_real[i] * fft_real[i] + fft_imag[i] * fft_imag[i]);
        total += magnitudes[i];
    }
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

    static float fft_real[FFT_BUF_SIZE];
    static float fft_imag[FFT_BUF_SIZE];
    fft_samples(samples, fft_real, fft_imag, n_samples);
    process_fft(ctx->fft_magnitudes, fft_real, fft_imag, n_samples);

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
    printf("    --pw-source/-s\n    \tint, PipeWire node for source audio from, see --pw-list-nodes\n");
    printf("    --pw-list-nodes\n    \tlist all PipeWire nodes\n");
}

int cli_parse(int argc, char **argv, opts_t *opts) {
    for (int i = 0; i < argc; i++) {
        char *arg = argv[i];

        if (!strcmp(arg, "--help")) {
            print_help(argv[0]);
            return 0;
        }

        if (!strcmp(arg, "--pw-list-nodes")) {
            print_pw_nodes(argc, argv);
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
    float fft_magnitudes_buffer[FFT_BUF_SIZE];

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
        .fft_magnitudes = fft_magnitudes_buffer,
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
            "audio-visualizer",
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
