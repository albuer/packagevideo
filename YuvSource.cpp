#include "YuvSource.h"

#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>

extern int32_t gNumFramesOutput;
namespace android {

YuvSource::YuvSource(int width, int height, int nFrames, int fps, int colorFormat, const char* filename)
    : mWidth(width),
      mHeight(height),
      mMaxNumFrames(nFrames),
      mFrameRate(fps),
      mColorFormat(colorFormat),
      mSize((width * height * 3) / 2) {

    mGroup.add_buffer(new MediaBuffer(mSize));
    if (filename != NULL) {
        mFile = fopen(filename, "rb");
    }
}

YuvSource::~YuvSource() {
    if (mFile != NULL) {
        fclose(mFile);
    }
}

sp<MetaData> YuvSource::getFormat() {
    sp<MetaData> meta = new MetaData;
    meta->setInt32(kKeyWidth, mWidth);
    meta->setInt32(kKeyHeight, mHeight);
    meta->setInt32(kKeyColorFormat, mColorFormat);
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);

    return meta;
}

status_t YuvSource::start(MetaData *params __unused) {
    mNumFramesOutput = 0;
    gNumFramesOutput = 0;
    return OK;
}

status_t YuvSource::stop() {
    gNumFramesOutput = mNumFramesOutput;
    return OK;
}

status_t YuvSource::read(
        MediaBuffer **buffer, const MediaSource::ReadOptions *options __unused) {

    if (mNumFramesOutput % 10 == 0) {
        fprintf(stderr, ".");
    }
    if (mNumFramesOutput == mMaxNumFrames) {
        return ERROR_END_OF_STREAM;
    }

    status_t err = mGroup.acquire_buffer(buffer);
    if (err != OK) {
        return err;
    }

    if (mFile) {
        int len = fread((*buffer)->data(), 1, mSize, mFile);
//            printf("read len: %zu %d\n", mSize, len);
        if (len <= 0) {
            printf("end of stream\n");
            return ERROR_END_OF_STREAM;
        }
    }

    (*buffer)->set_range(0, mSize);
    (*buffer)->meta_data()->clear();
    (*buffer)->meta_data()->setInt64(
            kKeyTime, (mNumFramesOutput * 1000000) / mFrameRate);
    ++mNumFramesOutput;

    return OK;
}

}  // namespace android
