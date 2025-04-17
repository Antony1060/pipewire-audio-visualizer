CFLAGS=-ggdb -Wall -Wextra -Werror -lm $(shell pkg-config --cflags --libs libpipewire-0.3 raylib)

main: main.c fft.c
	$(CC) $(CFLAGS) -O3 -Wall -Werror -Wextra main.c -o main

clean:
	rm main
