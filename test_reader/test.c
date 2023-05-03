#include "libnmf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]){
	struct nmf_container info;
	if (argc == 1 || argv[1][0] == '\0') {
		printf("FIXME! error while opening file.\n");
		exit(1);
	}
	FILE* fd = fopen(argv[1], "rb");
	if (fd == NULL) {
		printf("FIXME! error while opening file.\n");
		exit(1);
	}

	uint64_t size = read_nmf(fd, &info);

	printf("file size:%lld\n", size);

	printf("header:\n");
	printf("duration:%lf\n", info.header.duration);
	printf("track_num:%d\n", info.header.track_num);

	struct jfif_container video_info;
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
	struct nmf_cluster buffer;

//	FILE* fd_wr = fopen("test_output.flac", "wb");
//	const unsigned char attachment[] = {0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x12, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x33, 0x5f, 0x0b, 0xb8, 0x02, 0xf0, 0x12, 0x4c, 0x4e, 0x00, 0xbd, 0x4c, 0x74, 0xb3, 0x99, 0xb7, 0xfb, 0x2c, 0x58, 0x1f, 0xf7, 0x81, 0x2f, 0xee, 0x9d, 0xfa};
//	fwrite(attachment, 1, sizeof(attachment), fd_wr);
//	FILE* fd_wr = fopen("test_output.vbin", "wb");

	int biggest = 0;
	for(int i = 0; i < info.index.count; i++) {
		read_nmf_cluster(fd, &buffer);
		printf("block %d\n", i);
		printf("stamp:%lld (ns)\n", (uint64_t)buffer.header.stamp * info.index.scale);
		printf("frame_num:%d\n", buffer.header.frame_num);
		for (int j = 0; j < buffer.header.frame_num; j++) {
			printf("\tframe %d:\n", j);
			printf("\tframe tag: %x:\n", buffer.frames[j].tag);
			uint8_t id = buffer.frames[j].tag;
			uint32_t length = buffer.frames[j].tag >> 8;
			if (info.tracks[id].header.type == NMF_TRACK_VIDEO) {
//				char filename[256];
//				sprintf(filename, "output%d.bin", i);
//				FILE* fd_wr = fopen(filename, "wb");
//				fwrite(buffer.frames[j].payload, 1, length, fd_wr);
				biggest = length > biggest ? length : biggest;
			}
			free(buffer.frames[j].payload);
		}
		free(buffer.frames);
	}
	printf("biggest: %d\n", biggest);
//	fclose(fd_wr);
	return 0;
}
