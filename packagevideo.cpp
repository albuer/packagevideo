/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include <binder/ProcessState.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/AudioPlayer.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaCodecSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MPEG4Writer.h>
#include <media/MediaPlayerInterface.h>

#include <OMX_Video.h>

#include "YuvSource.h"
#include "AvcSource.h"

using namespace android;

int32_t gNumFramesOutput = 0;

static const uint32_t kMinBitRate = 100000;         // 0.1Mbps
static const uint32_t kMaxBitRate = 200 * 1000000;  // 200Mbps
static const int32_t kMaxTimeLimitSec = 180;       // 3 minutes

float gFrameRate = 30;
uint32_t gVideoWidth = 176;
uint32_t gVideoHeight = 144;
uint32_t gBitRate = 300000;
int gIFInterval = 1;
int gColorFormat = OMX_COLOR_FormatYUV420Planar;
int gFrameLimit = 30000;
int gLevel = -1;        // Encoder specific default
int gProfile = -1;      // Encoder specific default
int gOutCodec = 1;
int gInCodec  = 0;
int gTimeLimitSec = 60;
const char *gOutFileName = "/sdcard/output.mp4";
const char *gInFileName = NULL;
bool gPreferSoftwareCodec = false;

// Print usage showing how to use this utility to record videos
static void usage(const char *me) {
    fprintf(stderr,
        "Usage: %s [options] <filename>\n"
        "\n"
        "Options:\n"
        "--size WIDTHxHEIGHT\n"
        "    Set the video size, Default is %dx%d.\n"
        "--bit-rate RATE\n"
        "    Set the video bit rate, in bits per second.  Value may be specified as\n"
        "    bits or megabits, e.g. '4000000' is equivalent to '4M'. Default is %d.\n"
        "--frame-rate RATE\n"
        "    Set the video frame rate per second. Default is %f.\n"
        "--iframe-interval TIME\n"
        "    Set the i-frame interval, in seconds.  Default is %d.\n"
        "--profile Profile\n"
        "    Set the profile: [baseline] [main] [high].  Default is %d.\n"
        "--level Level\n"
        "    Set the level: [1/1b/1.1/1.2/1.3] [2/2.1/2.2] [3/3.1/3.2] [4/4.1/4.2] [5/5.1/5.2].\n"
        "    Default is %d.\n"
        "--color Color\n"
        "    YUV420 color format: [0] semi planar or [1] planar or other omx YUV420 color format\n"
        "    Default is 1\n"
        "--time-limit TIME\n"
        "    Set the maximum recording time, in seconds.  Default / maximum is %d.\n"
        "--frame-limit Frames\n"
        "    Set the maximum recording frames. Default / maximum is %d.\n"
        "--soft-prefer\n"
        "    Prefer software codec for encode\n"
        "--out-vcodec\n"
        "    Output video codec: [1] AVC [2] M4V [3] H263. Need input video codec YUV. Default is %d.\n"
        "--in-vcodec\n"
        "    Input video codec: [0] YUV [1] AVC. Default is %d.\n"
        "--output FILENAME\n"
        "    Output file. Default is %s\n"
        "--input FILENAME\n"
        "    Input file for encode and/or package.\n"
        "--help\n"
        "    Show this message.\n"
        "\n",
        me, gVideoWidth, gVideoHeight, gBitRate, gFrameRate, gIFInterval,
        gProfile, gLevel, gTimeLimitSec, gFrameLimit,
        gOutCodec, gInCodec, gOutFileName
        );
    exit(1);
}

enum {
    kYUV420SP = 0,
    kYUV420P  = 1,
};

enum {
    kCodecYUV = 0,
    kCodecAVC = 1,
    kCodecM4V = 2,
    kCodecH263 = 3,
};

static const char* codecName[] = {
    "YUV", "AVC", "M4V", "H264"
};

// returns -1 if mapping of the given color is unsuccessful
// returns an omx color enum value otherwise
static int translateColorToOmxEnumValue(int color) {
    switch (color) {
        case kYUV420SP:
            return OMX_COLOR_FormatYUV420SemiPlanar;
        case kYUV420P:
            return OMX_COLOR_FormatYUV420Planar;
        default:
            fprintf(stderr, "Custom OMX color format: %d\n", color);
            if (color == OMX_TI_COLOR_FormatYUV420PackedSemiPlanar ||
                color == OMX_QCOM_COLOR_FormatYVU420SemiPlanar) {
                return color;
            }
    }
    return -1;
}

/*
 * Parses a string of the form "1280x720".
 *
 * Returns true on success.
 */
static bool parseWidthHeight(const char* widthHeight, uint32_t* pWidth,
        uint32_t* pHeight) {
    long width, height;
    char* end;

    // Must specify base 10, or "0x0" gets parsed differently.
    width = strtol(widthHeight, &end, 10);
    if (end == widthHeight || *end != 'x' || *(end+1) == '\0') {
        // invalid chars in width, or missing 'x', or missing height
        return false;
    }
    height = strtol(end + 1, &end, 10);
    if (*end != '\0') {
        // invalid chars in height
        return false;
    }

    *pWidth = width;
    *pHeight = height;
    return true;
}

/*
 * Accepts a string with a bare number ("4000000") or with a single-character
 * unit ("4m").
 *
 * Returns an error if parsing fails.
 */
static status_t parseValueWithUnit(const char* str, uint32_t* pValue) {
    long value;
    char* endptr;

    value = strtol(str, &endptr, 10);
    if (*endptr == '\0') {
        // bare number
        *pValue = value;
        return NO_ERROR;
    } else if (toupper(*endptr) == 'K' && *(endptr+1) == '\0') {
        *pValue = value * 1000;  // check for overflow?
        return NO_ERROR;
    } else if (toupper(*endptr) == 'M' && *(endptr+1) == '\0') {
        *pValue = value * 1000000;  // check for overflow?
        return NO_ERROR;
    } else {
        fprintf(stderr, "Unrecognized value: %s\n", str);
        return UNKNOWN_ERROR;
    }
}

static status_t parseLevel(const char* str, int32_t* pValue) {
    if (strcmp(str, "1") == 0)
        *pValue = OMX_VIDEO_AVCLevel1;
    else if (strcmp(str, "1b") == 0)
        *pValue = OMX_VIDEO_AVCLevel1b;
    else if (strcmp(str, "1.1") == 0)
        *pValue = OMX_VIDEO_AVCLevel11;
    else if (strcmp(str, "1.2") == 0)
        *pValue = OMX_VIDEO_AVCLevel12;
    else if (strcmp(str, "1.3") == 0)
        *pValue = OMX_VIDEO_AVCLevel13;
    else if (strcmp(str, "2") == 0)
        *pValue = OMX_VIDEO_AVCLevel2;
    else if (strcmp(str, "2.1") == 0)
        *pValue = OMX_VIDEO_AVCLevel21;
    else if (strcmp(str, "2.2") == 0)
        *pValue = OMX_VIDEO_AVCLevel22;
    else if (strcmp(str, "3") == 0)
        *pValue = OMX_VIDEO_AVCLevel3;
    else if (strcmp(str, "3.1") == 0)
        *pValue = OMX_VIDEO_AVCLevel31;
    else if (strcmp(str, "3.2") == 0)
        *pValue = OMX_VIDEO_AVCLevel32;
    else if (strcmp(str, "4") == 0)
        *pValue = OMX_VIDEO_AVCLevel4;
    else if (strcmp(str, "4.1") == 0)
        *pValue = OMX_VIDEO_AVCLevel41;
    else if (strcmp(str, "4.2") == 0)
        *pValue = OMX_VIDEO_AVCLevel42;
    else if (strcmp(str, "5") == 0)
        *pValue = OMX_VIDEO_AVCLevel5;
    else if (strcmp(str, "5.1") == 0)
        *pValue = OMX_VIDEO_AVCLevel51;
    else if (strcmp(str, "5.2") == 0)
        *pValue = OMX_VIDEO_AVCLevel52;

    return NO_ERROR;
}

int main(int argc, char **argv) {
    static const struct option longOptions[] = {
        { "help",               no_argument,        NULL, 'h' },
        { "size",               required_argument,  NULL, 's' },
        { "bit-rate",           required_argument,  NULL, 'b' },
        { "time-limit",         required_argument,  NULL, 't' },
        { "frame-rate",         required_argument,  NULL, 'a' },
        { "iframe-interval",    required_argument,  NULL, 'e' },
        { "profile",            required_argument,  NULL, 'p' },
        { "level",              required_argument,  NULL, 'l' },
        { "color",              required_argument,  NULL, 'c' },
        { "frame-limit",        required_argument,  NULL, 'n' },
        { "soft-prefer",        no_argument,        NULL, 'q' },
        { "out-vcodec",         required_argument,  NULL, 'w' },
        { "in-vcodec",          required_argument,  NULL, 'x' },
        { "output",             required_argument,  NULL, 'o' },
        { "input",              required_argument,  NULL, 'i' },
        { NULL,                 0,                  NULL, 0 }
    };

    android::ProcessState::self()->startThreadPool();

    while (true) {
        int optionIndex = 0;
        int ic = getopt_long(argc, argv, "", longOptions, &optionIndex);
        if (ic == -1) {
            break;
        }

        switch (ic) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 's':
            if (!parseWidthHeight(optarg, &gVideoWidth, &gVideoHeight)) {
                fprintf(stderr, "Invalid size '%s', must be width x height\n",
                        optarg);
                return 2;
            }
            if (gVideoWidth == 0 || gVideoHeight == 0) {
                fprintf(stderr,
                    "Invalid size %ux%u, width and height may not be zero\n",
                    gVideoWidth, gVideoHeight);
                return 2;
            }
            break;
        case 'b':
            if (parseValueWithUnit(optarg, &gBitRate) != NO_ERROR) {
                return 2;
            }
            if (gBitRate < kMinBitRate || gBitRate > kMaxBitRate) {
                fprintf(stderr,
                        "Bit rate %dbps outside acceptable range [%d,%d]\n",
                        gBitRate, kMinBitRate, kMaxBitRate);
                return 2;
            }
            break;
        case 't':
            gTimeLimitSec = atoi(optarg);
            if (gTimeLimitSec == 0 || gTimeLimitSec > kMaxTimeLimitSec) {
                fprintf(stderr,
                        "Time limit %ds outside acceptable range [1,%d]\n",
                        gTimeLimitSec, kMaxTimeLimitSec);
                return 2;
            }
            break;
        case 'a':
            gFrameRate = atof(optarg);
            break;
        case 'e':
            gIFInterval = atoi(optarg);
            break;
        case 'p':// -p main
            if (strcmp(optarg, "baseline") == 0) {
                gProfile = OMX_VIDEO_AVCProfileBaseline;
            } else if (strcmp(optarg, "main") == 0) {
                gProfile = OMX_VIDEO_AVCProfileMain;
            } else if (strcmp(optarg, "high") == 0) {
                gProfile = OMX_VIDEO_AVCProfileHigh;
            }
            break;
        case 'l':// -l 5.1
            parseLevel(optarg, &gLevel);
            break;
        case 'c':
            gColorFormat = translateColorToOmxEnumValue(atoi(optarg));
            if (gColorFormat == -1) {
                usage(argv[0]);
            }
            break;
        case 'n':
            gFrameLimit = atoi(optarg);
            break;
        case 'q':
            gPreferSoftwareCodec = true;
            break;
        case 'w':
            gOutCodec = atoi(optarg);
            if (gOutCodec < 1 || gOutCodec > 3) {
                usage(argv[0]);
            }
            break;
        case 'x':
            gInCodec = atoi(optarg);
            if (gInCodec < 0 || gInCodec > 1) {
                usage(argv[0]);
            }
            break;
        case 'o':
            gOutFileName = optarg;
            break;
        case 'i':
            gInFileName = optarg;
            break;
        default:
            if (ic != '?') {
                fprintf(stderr, "getopt_long returned unexpected value 0x%x\n", ic);
            }
            return 2;
        }
    }

    if (gInFileName == NULL) {
        fprintf(stderr, "Please special input file\n");
        return 3;
    }

        printf("Input\n");
        printf("\tFilename: %s\n", gInFileName);
        printf("\tSize: %dx%d\n", gVideoWidth, gVideoHeight);
        printf("\tInput video codec: %s\n", codecName[gInCodec]);
        printf("\n");
        printf("Output\n");
        printf("\tFilename: %s\n", gOutFileName);
        printf("\tOutput video codec: %s\n", codecName[gOutCodec]);
        printf("\tColor format: %d\n", gColorFormat);
        if (gInCodec == kCodecYUV) {
            printf("\tBit rate: %d\n", gBitRate);
            printf("\tFrame rate: %.1f\n", gFrameRate);
            printf("\tI Frame interval: %d s\n", gIFInterval);
            printf("\tProfile: %d\n", gProfile);
            printf("\tLevel: %d\n", gLevel);
            if (gPreferSoftwareCodec) printf("\tPrefer software codec\n");
        }

    status_t err = OK;
    sp<IMediaSource> encoder;
    sp<MediaSource> source;
    if (gInCodec == kCodecYUV) {
        // input video format is YUV, require encoder
        source = new YuvSource(gVideoWidth, gVideoHeight, gFrameLimit, gFrameRate, gColorFormat, gInFileName);
        sp<AMessage> enc_meta = new AMessage;
        switch (gOutCodec) {
            case kCodecM4V:
                enc_meta->setString("mime", MEDIA_MIMETYPE_VIDEO_MPEG4);
                break;
            case kCodecH263:
                enc_meta->setString("mime", MEDIA_MIMETYPE_VIDEO_H263);
                break;
            default:
                enc_meta->setString("mime", MEDIA_MIMETYPE_VIDEO_AVC);
                break;
        }
        enc_meta->setInt32("width", gVideoWidth);
        enc_meta->setInt32("height", gVideoHeight);
        enc_meta->setInt32("frame-rate", gFrameRate);
        enc_meta->setInt32("bitrate", gBitRate);
        enc_meta->setInt32("stride", gVideoWidth);
        enc_meta->setInt32("slice-height", gVideoHeight);
        enc_meta->setInt32("i-frame-interval", gIFInterval);
        enc_meta->setInt32("color-format", gColorFormat);
        if (gLevel != -1) {
            enc_meta->setInt32("level", gLevel);
        }
        if (gProfile != -1) {
            enc_meta->setInt32("profile", gProfile);
        }

        sp<ALooper> looper = new ALooper;
        looper->setName("packagevideo");
        looper->start();

        encoder = MediaCodecSource::Create(
                    looper, enc_meta, source, NULL /* consumer */,
                    gPreferSoftwareCodec ? MediaCodecSource::FLAG_PREFER_SOFTWARE_CODEC : 0);
    } else if (gInCodec == kCodecAVC) {
        // input video format is AVC, no encoder required
        encoder = source = new AvcSource(gVideoWidth, gVideoHeight, gFrameLimit, gFrameRate, gColorFormat, gInFileName);
    }

    int fd = open(gOutFileName, O_CREAT | O_LARGEFILE | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        fprintf(stderr, "couldn't open file");
        return 1;
    }
    sp<MPEG4Writer> writer = new MPEG4Writer(fd);
    close(fd);
    writer->addSource(encoder);
    int64_t start = systemTime();
    CHECK_EQ((status_t)OK, writer->start());
    while (!writer->reachedEOS()) {
        usleep(100000);
    }
    source->stop();
    err = writer->stop();
    int64_t end = systemTime();

    fprintf(stderr, "$\n");

    if (err != OK && err != ERROR_END_OF_STREAM) {
        fprintf(stderr, "record failed: %d\n", err);
        return 1;
    }
    fprintf(stderr, "encoding %d frames in %" PRId64 " us\n", gNumFramesOutput, (end-start)/1000);
    fprintf(stderr, "encoding speed is: %.2f fps\n", (gNumFramesOutput * 1E9) / (end-start));
    return 0;
}
