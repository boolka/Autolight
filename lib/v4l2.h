#ifndef V4L2_H
#define V4L2_H

#define BUFFERS_MAX_COUNT 2
#define DEFAULT_CAPTURE_WIDTH 640
#define DEFAULT_CAPTURE_HEIGHT 480
#define DEFAULT_DEVICE_TMP "/dev/video%d"
#define DEFAULT_DEVICE_TMPLEN 11
#define DEFAULT_DEVICE_MAXNUM 10
#define DEVICE_NAME_MAXLEN 64
#define DEFAULT_PIXEL_FORMAT V4L2_PIX_FMT_MJPEG

struct buffers {
	void* start;
	size_t length;
};

void open_device(char*);
int init_device(void);
void close_device(void);
void init_mmap(void);
void start_capturing(void);
void read_frame(unsigned char*);

#endif