CFLAGS=-O0 -ggdb -Wall -Wextra -Werror -lm $(shell pkg-config --cflags --libs libpipewire-0.3 raylib)

main: main.c fft.c
	$(CC) $(CFLAGS) main.c -o main

clean:
	rm main
