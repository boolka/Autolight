#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <math.h>
#include "lib/v4l2.h"
#include "lib/xws.h"

#define CLOCK_TO_MS(clock) (((clock) * 1000) / CLOCKS_PER_SEC)

#define STD_RGB_TO_BRIGHTNESS(R,G,B) (0.2126*(R) + 0.7152*(G) + 0.0722*(B))
#define OPT1_RGB_TO_BRIGHTNESS(R,G,B) (0.299*(R) + 0.587*(G) + 0.114*(B))
#define OPT2_RGB_TO_BRIGHTNESS(R,G,B) (sqrt(0.299*pow((R), 2) + 0.587*pow((G), 2) + 0.114*pow((B), 2)))

#define DEFAULT_CALIBRATE_FRAMES 24
#define DEFAULT_INTERACTIVE_TIMEOUT 1000

enum OPTIONS {
	HELP_OPTION,
	DEVICE_OPTION,
	DISPLAY_OPTION,
	WIDTH_OPTION,
	HEIGHT_OPTION,
	CALIBRATE_TIMES_OPTION,
	BRIGHTNESS_OPTION,
	INTERACTIVE_OPTION,
	UNRECOGNIZED_OPTION
};

enum BRIGHTNESS_ALGORITHM_OPTIONS {
	BRIGHTNESS_ALGORITHM_STD,
	BRIGHTNESS_ALGORITHM_OPT1,
	BRIGHTNESS_ALGORITHM_OPT2
};

extern int capture_width;
extern int capture_height;

/**
h - help
d - device
c - calibrate
	Calibrate frames
x - brightness algorithm
i - interactive
 */
static char* short_options = "hd:c:x:i::";
// The sequence of this array must match enum OPTIONS.
static struct option long_options[9] = {
	{
		"help",
		no_argument,
		NULL, 0
	},
	{
		"device",
		required_argument,
		NULL, 0
	},
	{
		"display",
		required_argument,
		NULL, 0
	},
	{
		"width",
		required_argument,
		NULL, 0
	},
	{
		"height",
		required_argument,
		NULL, 0
	},
	{
		"calibrate",
		required_argument,
		NULL, 0
	},
	{
		"brightness",
		required_argument,
		NULL, 0
	},
	{
		"interactive",
		optional_argument,
		NULL, 0
	},
	{0}
};

static char* help_msg = "Autolight. Cameras driver must support MJPEG video streaming type and memory MMAP.\n\
-h (--help) This message.\n\
-d (--device=DEVICE_FILE) Video camera device file. \"/dev/video(0-9)\" by default.\n\
--display=DISPLAY_NAME Display name. By default used $DISPLAY from envs.\n\
--width=VALUE Camera capture width(640px default).\n\
--height=VALUE Camera capture height(480px default).\n\
-c (--calibrate=VALUE) Frames used to calibrate camera exposure. Only if camera supports V4L2_EXPOSURE_AUTO, \
V4L2_EXPOSURE_SHUTTER_PRIORITY or V4L2_EXPOSURE_APERTURE_PRIORITY auto type. Ignored otherwise.\n\
-x (--brightness=[STD|OPT1|OPT2]) Algorithm to calculate delta brightness.\n\
\tSTD: 0.2126 * R + 0.7152 * G + 0.0722 * B\n\
\tOPT1: 0.299 * R + 0.587 * G + 0.114 * B\n\
\tOPT2: sqrt(0.299 * R^2 + 0.587 * G^2 + 0.114 * B^2\n";

static char display_name[32] = {0};
static char* device_name = NULL;
static int calibrate_frames = DEFAULT_CALIBRATE_FRAMES;
static int interactive_timeout = DEFAULT_INTERACTIVE_TIMEOUT;
static int brightness_algo = BRIGHTNESS_ALGORITHM_STD;
static int auto_exposure = 0;
static int interactive = 0;

static int set_options(enum OPTIONS option) {
	switch (option) {
		case HELP_OPTION: {
			puts(help_msg);
			exit(EXIT_SUCCESS);
			break;
		}
		case DEVICE_OPTION: {
			if (optarg != 0) {
				device_name = malloc(strlen(optarg) + 1);
				if (NULL == device_name) {
					fprintf(stderr, "Out of memory\n");
					exit(EXIT_FAILURE);
				}
				strcpy(device_name, optarg);
			}
			break;
		}
		case DISPLAY_OPTION: {
			if (optarg != 0) {
				strcpy(display_name, optarg);
			}
			break;
		}
		case WIDTH_OPTION: {
			capture_width = atoi(optarg);
			break;
		}
		case HEIGHT_OPTION: {
			capture_height = atoi(optarg);
			break;
		}
		case CALIBRATE_TIMES_OPTION: {
			calibrate_frames = atoi(optarg);
			break;
		}
		case BRIGHTNESS_OPTION: {
			if (strcmp(optarg, "opt1") == 0 || strcmp(optarg, "OPT1") == 0) {
				brightness_algo = BRIGHTNESS_ALGORITHM_OPT1;
			} else if (strcmp(optarg, "opt2") == 0 || strcmp(optarg, "OPT2") == 0) {
				brightness_algo = BRIGHTNESS_ALGORITHM_OPT2;
			}
			break;
		}
		case INTERACTIVE_OPTION: {
			interactive = 1;
			if (optarg != 0) {
				interactive_timeout = atoi(optarg);
			}
			break;
		}
		case UNRECOGNIZED_OPTION: {
			return -1;
		}
		default: {
			return -1;
		}
	}

	return 0;
}

// Returns value in range from 0 to 1.
static double image_brightness() {
	unsigned char* frame;
	unsigned char* pframe;
	unsigned char R, G, B;
	unsigned int frame_pixels = capture_height * capture_width;
	unsigned int frame_bytes = frame_pixels * 3;
	double delta_brightness = 0.0;

	frame = malloc(frame_bytes);

	if (NULL == frame) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	pframe = frame;
	read_frame(frame);

	switch (brightness_algo) {
		case BRIGHTNESS_ALGORITHM_STD: {
			for (int i = 0; i < frame_pixels; i++) {
				R = *pframe++;
				G = *pframe++;
				B = *pframe++;
				delta_brightness += STD_RGB_TO_BRIGHTNESS(R, G, B);
			}
			break;
		}
		case BRIGHTNESS_ALGORITHM_OPT1: {
			for (int i = 0; i < frame_pixels; i++) {
				R = *pframe++;
				G = *pframe++;
				B = *pframe++;
				delta_brightness += OPT1_RGB_TO_BRIGHTNESS(R, G, B);
			}
			break;
		}
		case BRIGHTNESS_ALGORITHM_OPT2: {
			for (int i = 0; i < frame_pixels; i++) {
				R = *pframe++;
				G = *pframe++;
				B = *pframe++;
				delta_brightness += OPT2_RGB_TO_BRIGHTNESS(R, G, B);
			}
			break;
		}
	}

	free(frame);
	return (delta_brightness / (capture_width * capture_height))/UCHAR_MAX;
}

static void calibrate_cam() {
	unsigned char* frame;
#	ifdef DEBUG
	clock_t frame_start, frame_end, frame_avg;
	clock_t calibrate_start, calibrate_end;

	calibrate_start = clock();
#	endif

	if (!calibrate_frames) {
		return;
	}

	frame = malloc(capture_width * 3 * capture_height);

	if (NULL == frame) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < calibrate_frames; i++) {
#		ifdef DEBUG
		frame_start = clock();
#		endif
		read_frame(frame);
#		ifdef DEBUG
		frame_end = clock();
		frame_avg += frame_end - frame_start;
#		endif
	}
#	ifdef DEBUG
	calibrate_end = clock();

	frame_avg /= calibrate_frames;
	printf("Frame average time: %ldms\n", (long)CLOCK_TO_MS(frame_avg));
	printf("Calibrate time: %ldms\n", (long)CLOCK_TO_MS(calibrate_end - calibrate_start));
#	endif

	free(frame);
}

static void main_loop() {
	long brightness;

	if (auto_exposure) {
		calibrate_cam();
	}

	do {
		brightness = (long)(image_brightness() * 100);

#		ifdef DEBUG
		printf("Calculated brightness: %lu (100 max)\n", brightness);
#		endif
		if (xws_backlight_set(brightness) == -1) {
			fprintf(stderr, "Can't find any valid output(xcb)");
		}

		if (interactive && interactive_timeout) {
			// TODO: interactive timer
			// sleep(interactive_timeout);
		}
	} while (interactive);
}

int main(int argc, char* argv[]) {
	int option;
	enum OPTIONS long_option;
	int long_option_ind;

	// getopt() does not print an error message
	opterr = 0;

	while ((option = getopt_long(argc, argv, short_options, long_options, &long_option_ind)) != -1) {
		if (option == 0) {
			long_option = (enum OPTIONS)long_option_ind;
		} else {
			switch (option) {
				case 'h': {
					long_option = HELP_OPTION;
					break;
				}
				case 'd': {
					long_option = DEVICE_OPTION;
					break;
				}
				case 'c': {
					long_option = CALIBRATE_TIMES_OPTION;
					break;
				}
				case 'x': {
					long_option = BRIGHTNESS_OPTION;
					break;
				}
				case 'i': {
					long_option = INTERACTIVE_OPTION;
					break;
				}
				case '?': {
					long_option = UNRECOGNIZED_OPTION;
					break;
				}
			}
		}
		if (set_options(long_option) == -1) {
			if (long_option == UNRECOGNIZED_OPTION) {
				fprintf(stderr,"unrecognized option \"%c\"\n", (char)optopt);
			} else {
				fprintf(stderr, "set_options");
				exit(EXIT_FAILURE);	
			}
		}
	}

#	ifdef DEBUG
	if (*display_name == 0) {
		printf("Default display (%s)\n", getenv("DISPLAY"));
	} else {
		printf("Display name: %s\n", display_name);
	}
	printf("Capture width(requested): %dpx\n", capture_width);
	printf("Capture height(requested): %dpx\n", capture_height);
	printf("Calibrate exposure frames: %d\n", calibrate_frames);
	printf("Brightness algorithm: %s\n", brightness_algo == BRIGHTNESS_ALGORITHM_STD ? "STD_RGB_TO_BRIGHTNESS" :
			brightness_algo == BRIGHTNESS_ALGORITHM_OPT1 ? "OPT1_RGB_TO_BRIGHTNESS" : "OPT2_RGB_TO_BRIGHTNESS");
	if (interactive) {
		printf("Interactive mode with frequency: %dms\n", interactive_timeout);
	}
#	endif

    open_device(device_name);
	auto_exposure = init_device();

#	ifdef DEBUG
	printf("Auto exposure: %s\n", auto_exposure ? "on" : "off");
	printf("Capture width(recognized): %dpx\n", capture_width);
	printf("Capture height(recognized): %dpx\n", capture_height);
#	endif

	init_mmap();
	xws_init(display_name);
	start_capturing();
	main_loop();
	close_device();

	free(device_name);

    return EXIT_SUCCESS;
}