# `Autolight`
Is the little linux tool for X Window System to correct laptop backlight level based on environment lights level.
Also works well when gnome standard lightning slider doesn't works.
Cameras driver must support MJPEG video streaming type and memory MMAP.

```Compile with DEBUG env to provide additional output.```

### Usage
After install in gnome environments press Alt+F2 and enter autolight. Or the same in terminal.

### Dependencies
- libjpeg-dev
- libxcb-util-dev
- libxcb-randr0-dev

If you use `Ubuntu` just run in terminal:
```
sudo apt install libjpeg-dev libxcb-util-dev libxcb-randr0-dev
```
On other distros take care yourself.

### Install
```
./configure && make && sudo make install
```

### Uninstall
```
sudo make uninstall
```

### Options
- -h (--help) Help message.
- -d (--device=DEVICE_FILE) Video camera device file. Tested "/dev/video(0-9)" by default.
- --display=DISPLAY_NAME Display name. By default used $DISPLAY from envs.
- --width=VALUE Camera capture width(640px by default).
- --height=VALUE Camera capture height(480px by default).
- -c (--calibrate=VALUE) Frames used to calibrate camera exposure. Only if camera supports V4L2_EXPOSURE_AUTO, V4L2_EXPOSURE_SHUTTER_PRIORITY or V4L2_EXPOSURE_APERTURE_PRIORITY auto type. Ignored otherwise.
- -x (--brightness=[STD|OPT1|OPT2]) Algorithm to calculate delta brightness.
    - STD: 0.2126 * R + 0.7152 * G + 0.0722 * B
    - OPT1: 0.299 * R + 0.587 * G + 0.114 * B
    - OPT2: sqrt(0.299 * R^2 + 0.587 * G^2 + 0.114 * B^2

# Todos
### Add more pixel formats
### Add interactive mode & demonize
