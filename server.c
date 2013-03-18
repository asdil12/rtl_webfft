/*
 * rtlizer - a simple spectrum analyzer using rtlsdr
 * 
 * Copyright (C) 2013 Alexandru Csete
 * 
 * Includes code from rtl_test.c:
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * rtlizer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rtlizer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libfcd.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <rtl-sdr.h>
#include <stdlib.h>
#include <stdbool.h>

#include "kiss_fft.h"


#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>

#include <signal.h>

#include <time.h>

#include <fcntl.h>

#define DEFAULT_SAMPLE_RATE 2000000
#define DYNAMIC_RANGE 90.f  /* -dBFS coreresponding to bottom of screen */
#define SCREEN_FRAC 0.7f  /* fraction of screen height used for FFT */

uint8_t *buffer;
uint32_t dev_index = 0;
uint32_t frequency = 99400000;
uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
uint32_t buff_len = 2048;

//int fft_size = 256;
int fft_size = 1024;
kiss_fft_cfg  fft_cfg;
kiss_fft_cpx *fft_in;
kiss_fft_cpx *fft_out;
float         *log_pwr_fft; /* dbFS relative to 1.0 */
char          *log_pwr_fft_byte; /* fft line */
float scale;
int yzero = 0;
int text_margin = 0;

int listenfd = 0, connfd = 0;
bool connection_valid;


static rtlsdr_dev_t *dev = NULL;


static void destroy()
{
	close(connfd);
	close(listenfd);

   	rtlsdr_close(dev);
	free(buffer);

    free(fft_cfg);
    free(fft_in);
    free(fft_out);
    free(log_pwr_fft);
    free(log_pwr_fft_byte);
}


static void sighandler(int signum)
{
    fprintf(stderr, "Signal caught, exiting!\n");
	destroy();
	exit(2);
}

static void sighandler_pipe(int signum)
{
	connection_valid = false;
	close(connfd);
}

void error(const char *msg)
{
    perror(msg);
	destroy();
    exit(1);
}

static void setup_rtlsdr()
{
    int device_count;
    int r;

    buffer = malloc(buff_len * sizeof(uint8_t));

    device_count = rtlsdr_get_device_count();
    if (!device_count)
    {
        fprintf(stderr, "No supported devices found.\n");
        exit(1);
    }

    fprintf(stderr, "Using device %d: %s\n",
        dev_index, rtlsdr_get_device_name(dev_index));

    r = rtlsdr_open(&dev, dev_index);
    if (r < 0)
    {
        fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
        exit(1);
    }

    r = rtlsdr_set_sample_rate(dev, samp_rate);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to set sample rate.\n");

    r = rtlsdr_set_center_freq(dev, frequency);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to set center freq.\n");

    r = rtlsdr_set_tuner_gain_mode(dev, 0);
    if (r < 0)
		fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");

    r = rtlsdr_reset_buffer(dev);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to reset buffers.\n");

}

static bool read_rtlsdr()
{
    bool error = false;
    int n_read;
    int r;

    r = rtlsdr_read_sync(dev, buffer, buff_len, &n_read);
    if (r < 0) {
        fprintf(stderr, "WARNING: sync read failed.\n");
        error = true;
    }

    if ((uint32_t)n_read < buff_len) {
        fprintf(stderr, "Short read (%d / %d), samples lost, exiting!\n", n_read, buff_len);
        error = true;
    }

    return error;
}

static void run_fft()
{   
    int i;
    kiss_fft_cpx pt;
    float pwr;
    
    for (i = 0; i < fft_size; i++)
    {
        fft_in[i].r = ((float)buffer[2*i])/255.f;
        fft_in[i].i = ((float)buffer[2*i+1])/255.f;
    }
    kiss_fft(fft_cfg, fft_in, fft_out);
    for (i = 0; i < fft_size; i++)
    {
        /* shift, normalize and convert to dBFS */
        if (i < fft_size / 2)
        {
            pt.r = fft_out[fft_size/2+i].r / fft_size;
            pt.i = fft_out[fft_size/2+i].i / fft_size;
        }
        else
        {
            pt.r = fft_out[i-fft_size/2].r / fft_size;
            pt.i = fft_out[i-fft_size/2].i / fft_size;
        }
        pwr = pt.r * pt.r + pt.i * pt.i;
        
        log_pwr_fft[i] = 10.f * log10(pwr + 1.0e-20f);
		log_pwr_fft[i] += DYNAMIC_RANGE;
		if(log_pwr_fft[i] < 0) log_pwr_fft[i] = 0;
		log_pwr_fft[i] /= DYNAMIC_RANGE;
		log_pwr_fft[i] *= 255;
		log_pwr_fft_byte[i] = (char)log_pwr_fft[i];
    }
}

int main(int argc, char *argv[])
{
    struct sigaction sigact, sigact_pipe;
    sigact.sa_handler = sighandler;
    sigact_pipe.sa_handler = sighandler_pipe;
    sigemptyset(&sigact.sa_mask);
    sigemptyset(&sigact_pipe.sa_mask);
    sigact.sa_flags = 0;
    sigact_pipe.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGPIPE, &sigact_pipe, NULL);

    /* set up FFT */
    //fft_size = 2 * width/2;
    fft_cfg = kiss_fft_alloc(fft_size, false, NULL, NULL);
    fft_in = malloc(fft_size * sizeof(kiss_fft_cpx));
    fft_out = malloc(fft_size * sizeof(kiss_fft_cpx));
    log_pwr_fft = malloc(fft_size * sizeof(float));
    log_pwr_fft_byte = malloc(fft_size * sizeof(char));

	//unsigned int recvbuf[1];
	unsigned char recvbuf[1];
	char cmdbuf[32];
	unsigned short cmdbuf_pos = 0;

    setup_rtlsdr();


	struct sockaddr_in serv_addr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	char optval = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons((uint16_t)12345);

	bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

	listen(listenfd, 10);

	for(;;) {
		printf("listening...\n");
		connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); 
		printf("got connection\n");
		connection_valid = true;
		while(connection_valid) {

			/* set freqency */
			fcntl(connfd, F_SETFL, O_NONBLOCK);
			while(read(connfd, recvbuf, 1) > 0) {
				printf("reading command: %u\n", *recvbuf);
				if(*recvbuf == 0x00)
					cmdbuf_pos = 0;
				else if(*recvbuf == 0xFF && cmdbuf_pos > 0) {
					cmdbuf[cmdbuf_pos++] = 0x00;
					int newfreq = atoi(cmdbuf);
					frequency = (uint32_t)newfreq;
					printf("new freq: %iHz\n", newfreq);
					int r = rtlsdr_set_center_freq(dev, frequency);
					if (r < 0)
						fprintf(stderr, "WARNING: Failed to set center freq.\n");
				}
				else
					cmdbuf[cmdbuf_pos++] = (char)*recvbuf;
			}
			int opts = fcntl(connfd, F_GETFL);
			opts = opts & (~O_NONBLOCK);
			fcntl(connfd, F_SETFL, opts);

			/* get samples from rtlsdr */
			if (read_rtlsdr()) {
				destroy();
				return 1;  /* error reading -> exit */
			}

			/* calculate FFT */
			run_fft();

			write(connfd, log_pwr_fft_byte, fft_size * sizeof(char));
			//usleep(100000);
			usleep(500000);
		}
		close(connfd);
	}

	destroy();
    return 0;
}

