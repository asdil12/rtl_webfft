server: kiss_fft.c server.c
	gcc $+ -Wall -O2 -o $@ -lm `pkg-config --cflags --libs gtk+-2.0 librtlsdr`
