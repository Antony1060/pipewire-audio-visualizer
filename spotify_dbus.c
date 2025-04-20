#include<dbus/dbus.h>

typedef struct {
    char *artist;
    char *title;
} spotify_data_t;

int get_spotify_data(spotify_data_t *data) {
    // thank you mr. gpt
    DBusError err;
    DBusConnection* conn;
    DBusMessage* msg;
    DBusMessage* reply;
    DBusMessageIter args;
    //dbus_uint32_t serial = 0;

    dbus_error_init(&err);

    // Connect to the session bus
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return -1;
    }

    if (!conn)
        return -1;

    // Create method call to "org.freedesktop.DBus.Properties.Get"
    msg = dbus_message_new_method_call(
        "org.mpris.MediaPlayer2.spotify",    // target service
        "/org/mpris/MediaPlayer2",           // object path
        "org.freedesktop.DBus.Properties",   // interface
        "Get"                                // method
    );

    if (!msg) {
        fprintf(stderr, "Message Null\n");
        return -1;
    }

    // Add arguments: ("org.mpris.MediaPlayer2.Player", "Metadata")
    dbus_message_iter_init_append(msg, &args);

    const char* interface_name = "org.mpris.MediaPlayer2.Player";
    const char* property_name = "Metadata";

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface_name)) {
        return -1;
    }
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property_name)) {
        return -1;
    }

    // Send and wait for reply
    reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
    dbus_message_unref(msg); // done with the request

    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return -1;
    }

    if (!reply) {
        return -1;
    }

    // Parse the reply
    if (!dbus_message_iter_init(reply, &args)) {
        return -1;
    }

    if (DBUS_TYPE_VARIANT != dbus_message_iter_get_arg_type(&args)) {
        dbus_message_unref(reply);
        return -1;
    }

    DBusMessageIter variant;

    dbus_message_iter_recurse(&args, &variant);

    if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&variant)) {
        dbus_message_unref(reply);
        return -1;
    }

    DBusMessageIter dict_iter;
    dbus_message_iter_recurse(&variant, &dict_iter);

    while (dbus_message_iter_get_arg_type(&dict_iter) != DBUS_TYPE_INVALID) {
        DBusMessageIter entry_iter;
        dbus_message_iter_recurse(&dict_iter, &entry_iter);

        // Key
        const char* key = NULL;
        if (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&entry_iter, &key);
        }

        dbus_message_iter_next(&entry_iter);

        // Value
        if (dbus_message_iter_get_arg_type(&entry_iter) == DBUS_TYPE_VARIANT) {
            DBusMessageIter val_iter;
            dbus_message_iter_recurse(&entry_iter, &val_iter);

            if (strcmp(key, "xesam:title") == 0) {
                if (dbus_message_iter_get_arg_type(&val_iter) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&val_iter, &data->title);
            } else if (strcmp(key, "xesam:artist") == 0) {
                DBusMessageIter array_iter;
                dbus_message_iter_recurse(&val_iter, &array_iter);
                
                if (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&array_iter, &data->artist);
            }
        }

        dbus_message_iter_next(&dict_iter);
    }

    dbus_message_unref(reply);

    return 0;
}
