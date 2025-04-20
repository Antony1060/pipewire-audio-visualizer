CFLAGS=-O0 -ggdb -Wall -Wextra -Werror -lm $(shell pkg-config --cflags --libs libpipewire-0.3 raylib dbus-1)

main: main.c fft.c spotify_dbus.c
	$(CC) $(CFLAGS) main.c -o main

clean:
	rm main
