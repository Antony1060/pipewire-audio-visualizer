CFLAGS=-O3 -ggdb -Wall -Wextra -Werror -lm $(shell pkg-config --cflags --libs libpipewire-0.3 raylib dbus-1)

TARGET=./visualizer

.PHONY: default
default: $(TARGET)

$(TARGET): main.c fft.c spotify_dbus.c pipewire_enumerate.c ui.c
	$(CC) $(CFLAGS) main.c -o $@

clean:
	rm $(TARGET)
