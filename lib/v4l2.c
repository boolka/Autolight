#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <jpeglib.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include "v4l2.h"

int capture_width = DEFAULT_CAPTURE_WIDTH;
int capture_height = DEFAULT_CAPTURE_HEIGHT;

static struct buffers* buffers;
static int fd;
static int buffers_count;
static char device_name[DEVICE_NAME_MAXLEN];
static int auto_exposure_types[] = {V4L2_EXPOSURE_AUTO, V4L2_EXPOSURE_SHUTTER_PRIORITY, V4L2_EXPOSURE_APERTURE_PRIORITY};
static unsigned int pixel_format = DEFAULT_PIXEL_FORMAT;

static void errno_exit(const char *s) {
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static void dqbuf(struct v4l2_buffer* buf) {
	do {
		if (-1 == ioctl(fd, VIDIOC_DQBUF, buf)) {
			if (errno != EAGAIN) {
				errno_exit("VIDIOC_DQBUF");
			}
		} else {
			break;
		}
	} while(errno == EAGAIN || (buf->flags & V4L2_BUF_FLAG_ERROR));
}

static void qbuf(struct v4l2_buffer* buf) {
	if (-1 == ioctl(fd, VIDIOC_QBUF, buf)) {
		errno_exit("VIDIOC_QBUF");
	}
}

void open_device(char* name) {
	struct stat st;
    int def_name = 0;
    int stat_res;

    if (NULL == name) {
        def_name = 1;
        name = malloc(DEFAULT_DEVICE_TMPLEN + 1);

        if (NULL == name) {
            fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
        }

        for (int i = 0; i < DEFAULT_DEVICE_MAXNUM; i++) {
            sprintf(name, DEFAULT_DEVICE_TMP, i);

#           ifdef DEBUG
            printf("Trying to open %s as a video device...\n", name);
#           endif

            stat_res = stat(name, &st);

            if (stat_res == 0) {
#				ifdef DEBUG
				puts("Done");
#				endif
                break;
            } else if (-1 == stat_res && i == DEFAULT_DEVICE_MAXNUM - 1) {
                fprintf(stderr, "Can't identify default video device\n");
                exit(EXIT_FAILURE);
            }
        }
    } else {
        if (-1 == stat(name, &st)) {
            fprintf(stderr, "Can't identify '%s': %d, %s\n", name, errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", name);
		exit(EXIT_FAILURE);
	}

	fd = open(name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Can't open '%s': %d, %s\n", name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

    if (strlen(name) + 1 >= DEVICE_NAME_MAXLEN) {
        fprintf(stderr, "Device name %s is too long\n", name);
		exit(EXIT_FAILURE);
    } else {
        strcpy(device_name, name);
    }

    if (def_name) {
        free(name);
    }
}

static void set_format(void) {
	struct v4l2_format format;

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == ioctl(fd, VIDIOC_G_FMT, &format)) {
		errno_exit("VIDIOC_G_FMT");
	}

	// TODO: add various pixel formats

	format.fmt.pix.width = capture_width;
	format.fmt.pix.height = capture_height;
	format.fmt.pix.pixelformat = pixel_format;
	format.fmt.pix.field = V4L2_FIELD_NONE;

	if (-1 == ioctl(fd, VIDIOC_S_FMT, &format)) {
		fprintf(stderr, "Video cam don't support V4L2_PIX_FMT_MJPEG pixel format");
		errno_exit("VIDIOC_S_FMT");
	}

	capture_width = format.fmt.pix.width;
	capture_height = format.fmt.pix.height;
}

int init_device(void) {
    int auto_exposure = 0;
	struct v4l2_capability capabilities;
	struct v4l2_queryctrl queryctrl;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_control ctrl;

	if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &capabilities)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\n", device_name);
			exit(EXIT_FAILURE);
		} else {
            errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(capabilities.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\n", device_name);
		exit(EXIT_FAILURE);
	}

	if (!(capabilities.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "%s does not support streaming i/o\n", device_name);
		exit(EXIT_FAILURE);
	}

	set_format();

	memset(&cropcap, 0, sizeof(cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(fd, VIDIOC_CROPCAP, &cropcap) == 0) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;

		ioctl(fd, VIDIOC_S_CROP, &crop);
	}

	// Trying to turn on V4L2_CID_EXPOSURE_AUTO with some AUTO value, if this is possible.
	queryctrl.id = V4L2_CID_EXPOSURE_AUTO;
	if (-1 == ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if (errno == EINVAL) {
			// Driver don't support V4L2_CID_EXPOSURE_AUTO.
			auto_exposure = 0;
		} else {
			errno_exit("VIDIOC_QUERYCTRL");
		}
	} else {
		ctrl.id = V4L2_CID_EXPOSURE_AUTO;
		if (-1 == ioctl(fd, VIDIOC_G_CTRL, &ctrl)) {
			errno_exit("VIDIOC_G_CTRL");
		}  else {
			if (ctrl.value == V4L2_EXPOSURE_MANUAL) {
				for (int i = 0; i < 3; i++) {
					ctrl.value = auto_exposure_types[i];

					if (-1 == ioctl(fd, VIDIOC_S_CTRL, &ctrl)) {
						auto_exposure = 0;
						continue;
					}

					auto_exposure = 1;
					break;
				}
			} else {
                auto_exposure = 1;
            }

#			ifdef DEBUG
            if (auto_exposure) {
                printf("V4L2_CID_EXPOSURE_AUTO=%d\n", ctrl.value);
            }
#			endif

		}
	}

    return auto_exposure;
}

void close_device(void) {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &type)) {
		errno_exit("VIDIOC_STREAMOFF");
	}

	for (int i = 0; i < buffers_count; i++) {
		if (-1 == munmap(buffers[i].start, buffers[i].length)) {
			errno_exit("munmap");
		}
	}

	free(buffers);

	if (-1 == close(fd)) {
		errno_exit("close");
	}
}

void init_mmap(void) {
	struct v4l2_requestbuffers reqbuf;

	reqbuf.count = BUFFERS_MAX_COUNT;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support memory mapping", device_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (reqbuf.count == 0) {
		fprintf(stderr, "Insufficient buffer memory on %s\n", device_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(reqbuf.count, sizeof(*buffers));

	if (NULL == buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

#	ifdef DEBUG
	printf("Frame buffers allocated: %d\n", reqbuf.count);
#	endif

	for (buffers_count = 0; buffers_count < reqbuf.count; ++buffers_count) {
		struct v4l2_buffer buf_info;

		memset(&buf_info, 0, sizeof(buf_info));
		buf_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf_info.memory = V4L2_MEMORY_MMAP;
		buf_info.index = buffers_count;

		if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &buf_info)) {
			errno_exit("VIDIOC_QUERYBUF");
		}

		buffers[buffers_count].length = buf_info.length;
		buffers[buffers_count].start = mmap(NULL, buf_info.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf_info.m.offset);

		if (buffers[buffers_count].start == MAP_FAILED) {
			errno_exit("mmap");
		}
	}
}

void read_frame(unsigned char* frame) {
	struct v4l2_buffer buf;
#	ifdef DEBUG
	static int verified = 0;
	FILE* verification_img;
#	endif

	memset(&buf, 0, sizeof(struct v4l2_buffer));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	dqbuf(&buf);
	assert(buf.index < buffers_count);

	switch (pixel_format) {
		// TODO: add more pixel formats
		case V4L2_PIX_FMT_MJPEG: {
			int row_bytes, width, height, pixel_size;
			unsigned char* buffer_array[1];
			// Variables for the jpeg decompressor itself
			struct jpeg_decompress_struct cinfo;
			struct jpeg_error_mgr jerr;

#			ifdef DEBUG
			if (!verified) {
				verification_img = fopen("test.jpg", "wb");
				if (NULL == verification_img) {
					errno_exit("fopen");
				}
				if (1 != fwrite(buffers[buf.index].start, buf.bytesused, 1, verification_img)) {
					fprintf(stderr, "Can't write verifying image\n");
				}
				if (EOF == fclose(verification_img)) {
					errno_exit("fclose");
				}
				verified = 1;
			}
#			endif

			cinfo.err = jpeg_std_error(&jerr);
			jpeg_create_decompress(&cinfo);
			jpeg_mem_src(&cinfo, buffers[buf.index].start, buf.bytesused);

			jpeg_read_header(&cinfo, 1);
			jpeg_start_decompress(&cinfo);

			width = cinfo.output_width;
			height = cinfo.output_height;
			pixel_size = cinfo.output_components;
			row_bytes = width * pixel_size;

			buffer_array[0] = (unsigned char*)malloc(row_bytes);

			if (NULL == buffer_array[0]) {
				fprintf(stderr, "Out of memory\n");
				exit(EXIT_FAILURE);
			}

			while (cinfo.output_scanline < height) {
				jpeg_read_scanlines(&cinfo, buffer_array, 1);
				// Pixels follow in the format RGB
				memcpy(frame, buffer_array[0], row_bytes);
				frame += row_bytes;
			}

			free(buffer_array[0]);
			jpeg_finish_decompress(&cinfo);
			jpeg_destroy_decompress(&cinfo);
			break;
		}
	}
	qbuf(&buf);
}

void start_capturing(void) {
	struct v4l2_buffer buf;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	memset(&buf, 0, sizeof(struct v4l2_buffer));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	qbuf(&buf);

	if (-1 == ioctl(fd, VIDIOC_STREAMON, &type)) {
		errno_exit("VIDIOC_STREAMON");
	}
}
