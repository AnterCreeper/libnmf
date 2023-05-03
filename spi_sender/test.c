#include "libnmf.h"
#include "spidev.h"

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <errno.h>

#define MAX_TRANSFER_SIZE 16384

struct spidev{
	int fd;
        uint8_t bits;
        uint32_t mode;
        uint32_t speed;
	struct spi_ioc_transfer buf;
};

void spi_init(char* devname, struct spidev* dev) {
	dev->fd = open(devname, O_RDWR);
	if (dev->fd < 0) {
		printf("FIXME! can't open device!\n");
		exit(1);
	}

	/*
	 * spi mode
	 */
	if (ioctl(dev->fd, SPI_IOC_WR_MODE, &dev->mode) == -1) {
		printf("can't set spi write mode!\n");
		exit(-1);
	}
	if (ioctl(dev->fd, SPI_IOC_RD_MODE, &dev->mode) == -1) {
		printf("can't set spi read mode!\n");
		exit(-1);
	}
	/*
	 * bits per word
	 */
	if (ioctl(dev->fd, SPI_IOC_WR_BITS_PER_WORD, &dev->bits) == -1) {
		printf("can't set write bits per word!\n");
		exit(-1);
	}
	if (ioctl(dev->fd, SPI_IOC_RD_BITS_PER_WORD, &dev->bits) == -1) {
		printf("can't set read bits per word!\n");
		exit(-1);
	}
	/*
	 * max speed hz
	 */
	if (ioctl(dev->fd, SPI_IOC_WR_MAX_SPEED_HZ, &dev->speed) == -1) {
		printf("can't set write max speed!\n");
		exit(-1);
	}
	if (ioctl(dev->fd, SPI_IOC_RD_MAX_SPEED_HZ, &dev->speed) == -1) {
		printf("can't set read max speed!\n");
		exit(-1);
	}

	printf("spi device set successfully!\n");
	printf("spi mode:%x\n", dev->mode);
	printf("bits per word:%d\n", dev->bits);
	printf("max speed: %d MHz\n", dev->speed / 1000000);

	printf("buffer prepared!\n");
        dev->buf.delay_usecs = 0;
        dev->buf.speed_hz = dev->speed;
        dev->buf.bits_per_word = dev->bits;
	dev->buf.tx_nbits = 1;
	dev->buf.rx_buf = (unsigned int)NULL;
	return;
}

struct {
	FILE* fd;
	struct spidev dev;
	struct nmf_container* info;
	int i;
	int count;
	sem_t sem_eof;
} tx_worker_data;

#define unlikely(x) __builtin_expect(!!(x), 0)
#define min(x, y) unlikely((x) < (y)) ? (x) : (y)

//transfer only
void spi_transfer(struct spidev* dev, uint32_t* buffer, int32_t length) {
	if (buffer == NULL) {
		printf("FIXME! empty buffer!\n");
		exit(1);
	}
	int processed = 0;
	while (length - processed > 0) {
		int size = min(length - processed, MAX_TRANSFER_SIZE);
		dev->buf.len = size * 4;
		dev->buf.tx_buf = (unsigned int)&buffer[processed];
		dev->buf.rx_buf = (unsigned int)NULL;
		if (ioctl(dev->fd, SPI_IOC_MESSAGE(1), &dev->buf) < 1) {
			printf("FIXME! cannot send spi message!\n");
			printf("errno:%d %s\n", errno, strerror(errno));
			exit(1);
		}
		processed = processed + size;
	}
	return;
}

void tx_worker(){
	if (tx_worker_data.i < tx_worker_data.count) {
		struct nmf_cluster buffer;
		read_nmf_cluster(tx_worker_data.fd, &buffer); //not thread safe.
//		printf("block: %d\n", tx_worker_data.i);
//		printf("stamp:%d (secs)\n", buffer.header.stamp);
//		printf("frame_num:%d\n", buffer.header.frame_num);
		for (int j = 0; j < buffer.header.frame_num; j++) {
//			printf("\tframe %d:\n", j);
//			printf("\tframe tag: %x:\n", buffer.frames[j].tag);
			uint8_t id = buffer.frames[j].tag;
			uint32_t length = buffer.frames[j].tag >> 8;
			if (tx_worker_data.info->tracks[id].header.type == NMF_TRACK_VIDEO) {
				struct timeval tp;
				struct timezone tz;

				gettimeofday(&tp, &tz);
				int64_t time_pre = tp.tv_sec * 1000000 + tp.tv_usec;

				spi_transfer(&tx_worker_data.dev, buffer.frames[j].payload, (length + 3) / 4);

				gettimeofday(&tp, &tz);
				int64_t time_now = tp.tv_sec * 1000000 + tp.tv_usec;

				int delta = time_now - time_pre;
				if (unlikely(delta > 41666)) {
					printf("timeout!\n");
					printf("block=%d\n", tx_worker_data.i);
					printf("size=%d\n", length);
					printf("delta=%d(us)\n", delta);
					printf("bitrate=%lfMbps\n", (float)(length * 8) / 41666);
				}
			}
			free(buffer.frames[j].payload);
		}
		free(buffer.frames);
		tx_worker_data.i++; //not thread safe. however there is only 1 core.
	} else {
		sem_post(&tx_worker_data.sem_eof);
	}
        return;
}

int main(int argc, char* argv[]){
	if (argc == 1 || argv[1][0] == '\0') {
		printf("FIXME! no file args.\n");
		exit(1);
	}
	if (argc == 2 || argv[2][0] == '\0') {
		printf("FIXME! no dev args.\n");
		exit(1);
	}
	int spi_speed;
	if (argc == 3 || argv[3][0] == '\0') {
		spi_speed = 100000000;
		printf("using default speed: 100MHz\n");
	} else {
		spi_speed = atoi(argv[3]);
		printf("using user specifice speed: %dMHz\n", spi_speed / 1000000);
	}

	FILE* fd = fopen(argv[1], "rb");
	if (fd == NULL) {
		printf("FIXME! error while opening file.\n");
		exit(1);
	}

	struct spidev dh;
	dh.bits = 16;
	dh.mode = 0;
	dh.speed = spi_speed;
	spi_init(argv[2], &dh);

	struct nmf_container info;
	uint64_t size = read_nmf(fd, &info);

	printf("file size:%lld\n", size);

	printf("header:\n");
	printf("duration:%lf\n", info.header.duration);
	printf("track_num:%d\n", info.header.track_num);

	struct jfif_container video_info;
	int32_t clock_interval = 0;

	printf("track:\n");
	for (int i = 0; i < info.header.track_num; i++) {
		printf("track %d:\n", info.tracks[i].header.index);
		printf("track_type:%d\n", info.tracks[i].header.type);
		printf("track_codec:%x\n", info.tracks[i].header.codec);
		if (info.tracks[i].length != 0) {
			printf("track has attachment.\n");
			printf("length:%d\n", info.tracks[i].length);
			printf("content:\n\t");
			for (int j = 0; j < info.tracks[i].length; j++)
				printf("%x ", info.tracks[i].payload[j]);
			printf("\n");
		}
		if (info.tracks[i].header.type == NMF_TRACK_VIDEO) {
			if (info.tracks[i].header.codec == NMF_VIDEO_MJPG) {
				jfif_parse(&video_info, info.tracks[i].payload, info.tracks[i].length);
				printf("\tjfif:\n");
				printf("\twidth:%d, height:%d\n", video_info.width, video_info.height);
				printf("\tformat:%x\n", video_info.format);
				printf("\tinterval:%d\n", video_info.interval);
				clock_interval = video_info.interval;
			} else {
				printf("for now no support for any format expect MJPG!\n");
				exit(1);
			}
		}
		if (info.tracks[i].header.type == NMF_TRACK_AUDIO) {
			printf("for now no support for audio!\n");
		}
	}

	printf("index:\n");
	if(info.index.fp == 0) printf("no seek position.\n");
	else printf("seek position for cue: %x\n", info.index.fp);

	printf("timestamp scale factor:%d nanoseconds.\n", info.index.scale);
	printf("cluster counts:%d\n", info.index.count);

	printf("cluster:\n");

//	FILE* fd_wr = fopen("test_output.flac", "wb");
//	const unsigned char attachment[] = {0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x12, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x33, 0x5f, 0x0b, 0xb8, 0x02, 0xf0, 0x12, 0x4c, 0x4e, 0x00, 0xbd, 0x4c, 0x74, 0xb3, 0x99, 0xb7, 0xfb, 0x2c, 0x58, 0x1f, 0xf7, 0x81, 0x2f, 0xee, 0x9d, 0xfa};
//	fwrite(attachment, 1, sizeof(attachment), fd_wr);

//	FILE* fd_wr = fopen("test_output.vbin", "wb");

	tx_worker_data.fd = fd;
	tx_worker_data.info = &info;
	tx_worker_data.dev = dh;
	tx_worker_data.i = 0;
	tx_worker_data.count = info.index.count;

        sem_init(&tx_worker_data.sem_eof, 0, 0);

	if (clock_interval == 0) {
		printf("FIXME! no interval set!\n");
		exit(1);
	}

        timer_t timer;
        struct sigevent evp;
        memset(&evp, 0, sizeof(evp));
        evp.sigev_notify = SIGEV_THREAD;
        evp.sigev_notify_function = &tx_worker;
        evp.sigev_value.sival_ptr = &timer;
        evp.sigev_value.sival_int = 111; //whom a magic num!
        if (timer_create(CLOCK_MONOTONIC, &evp, &timer)) {
                printf("set tx worker failed.\n");
                exit(1);
        }

        struct itimerspec ts;
        ts.it_interval.tv_sec = 0;
        ts.it_interval.tv_nsec = clock_interval;
        ts.it_value.tv_sec = 0;
        ts.it_value.tv_nsec = clock_interval;
        if (timer_settime(timer, 0, &ts, NULL)) {
                printf("start timer failed.\n");
                exit(1);
        }

        sem_wait(&tx_worker_data.sem_eof);
        sem_destroy(&tx_worker_data.sem_eof);

	fclose(tx_worker_data.fd);
	close(dh.fd);
	return 0;
}
