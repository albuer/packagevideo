# PackageVideo
  一个命令行执行程序，调用系统的MediaSource/MPEG4Writer等接口，可以输入AVC/H264数据，不编码直接封装成MP4文件；或者输入YUV数据，经编码后封装成MP4文件。
  适用于Android平台

## 使用方法

* 命令行参数
```
Usage: ./packagevideo [options] <filename>

Options:
--size WIDTHxHEIGHT
    Set the video size, Default is 176x144.
--bit-rate RATE
    Set the video bit rate, in bits per second.  Value may be specified as
    bits or megabits, e.g. '4000000' is equivalent to '4M'. Default is 300000.
--frame-rate RATE
    Set the video frame rate per second. Default is 30.000000.
--iframe-interval TIME
    Set the i-frame interval, in seconds.  Default is 1.
--profile Profile
    Set the profile: [baseline] [main] [high].  Default is -1.
--level Level
    Set the level: [1/1b/1.1/1.2/1.3] [2/2.1/2.2] [3/3.1/3.2] [4/4.1/4.2] [5/5.1/5.2].
    Default is -1.
--color Color
    YUV420 color format: [0] semi planar or [1] planar or other omx YUV420 color format
    Default is 1
--time-limit TIME
    Set the maximum recording time, in seconds.  Default / maximum is 60.
--frame-limit Frames
    Set the maximum recording frames. Default / maximum is 30000.
--soft-prefer
    Prefer software codec for encode
--out-vcodec
    Output video codec: [1] AVC [2] M4V [3] H263. Need input video codec YUV. Default is 1.
--in-vcodec
    Input video codec: [0] YUV [1] AVC. Default is 0.
--output FILENAME
    Output file. Default is /sdcard/output.mp4
--input FILENAME
    Input file for encode and/or package.
--help
    Show this message.

```

* 输入YUV图像，编码输出AVC，并封装为MPEG4文件
```
 ./packagevideo --size 1920x1080 --bit-rate 1200k --frame-rate 24 --profile main --level 4.1 --output /sdcard/output.mp4 --input ./test.yuv
Input
	Filename: ./test.yuv
	Size: 1920x1080
	Input video codec: YUV

Output
	Filename: /sdcard/output.mp4
	Output video codec: AVC
	Color format: 19
	Bit rate: 1200000
	Frame rate: 24.0
	I Frame interval: 1 s
	Profile: 2
	Level: 4096
.....end of stream
$
encoding 42 frames in 2734921 us
encoding speed is: 15.36 fps
```

* 输入AVC图像，不经过编码，直接封装为MPEG4文件
```
./packagevideo --size 1920x1080 --out-vcodec 1 --in-vcodec 1 --output /sdcard/output.mp4 --input ./test.h264
Input
	Filename: ./test.h264
	Size: 1920x1080
	Input video codec: AVC

Output
	Filename: /sdcard/output.mp4
	Output video codec: AVC
	Color format: 19
.....end of stream
$
encoding 38 frames in 103766 us
encoding speed is: 366.21 fps
```

