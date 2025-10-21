#include<stdio.h>
#include<stdint.h>
#include<pipewire/pipewire.h>

// big thanks to https://docs.pipewire.org/page_tutorial2.html
//  and https://docs.pipewire.org/page_tutorial3.html
//
// idk why enumerating objects needs to be so verbose, holy shit

typedef struct {
    int pending;
    struct pw_main_loop *loop;
} roundtrip_data;

static void __on_core_done(void *_data, uint32_t id, int seq) {
    roundtrip_data *data = _data;

    if (id == PW_ID_CORE && seq == data->pending)
        pw_main_loop_quit(data->loop);
}

static void roundtrip(struct pw_core *core, struct pw_main_loop *loop) {
    static const struct pw_core_events core_events = {
        PW_VERSION_CORE_EVENTS,
        .done = __on_core_done,
    };

    roundtrip_data data = { .loop = loop };
    struct spa_hook core_listener;

    pw_core_add_listener(core, &core_listener, &core_events, &data);

    data.pending = pw_core_sync(core, PW_ID_CORE, 0);

    int err;
    if ((err = pw_main_loop_run(loop)) < 0)
        printf("error: pw_main_loop_run: %d\n", err);

    spa_hook_remove(&core_listener);
}

static void __registry_event_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
    (void) data; (void) permissions; (void) version;


    if (strcmp(type, "PipeWire:Interface:Node"))
        return;

    const char unknown_str[] = "unknown";

    const char *name = NULL;
    for (uint32_t i = 0; i < props->n_items; i++) {
        const struct spa_dict_item item = props->items[i];
        if (!strcmp(item.key, "node.name"))
            name = item.value;
    }

    if (!name)
        name = unknown_str;

    printf("%3u - %s\n", id, name);
}

void print_pw_nodes(int argc, char **argv) {
    printf("!! Note some of these might not be valid as a source\n");
    pw_init(&argc, &argv);

    struct pw_main_loop *loop = pw_main_loop_new(NULL);
    struct pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), NULL, 0);

    struct pw_core *core = pw_context_connect(context, NULL, 0);

    struct pw_registry *registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);

    static const struct pw_registry_events registry_events = {
        PW_VERSION_REGISTRY_EVENTS,
        .global = __registry_event_global,
    };

    struct spa_hook registry_listener;
    spa_zero(registry_listener);
    pw_registry_add_listener(registry, &registry_listener, &registry_events, NULL);

    roundtrip(core, loop);

    pw_proxy_destroy((struct pw_proxy*) registry);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
}
