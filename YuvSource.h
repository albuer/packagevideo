#ifndef YUV_SOURCE_H_

#define YUV_SOURCE_H_

#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <utils/Compat.h>

namespace android {

class YuvSource : public MediaSource {

public:
    YuvSource(int width, int height, int nFrames, int fps, int colorFormat, const char* filename);
    virtual sp<MetaData> getFormat();
    virtual status_t start(MetaData *params __unused);
    virtual status_t stop();
    virtual status_t read(
            MediaBuffer **buffer, const MediaSource::ReadOptions *options __unused);

protected:
    virtual ~YuvSource();

private:
    MediaBufferGroup mGroup;
    int mWidth, mHeight;
    int mMaxNumFrames;
    int mFrameRate;
    int mColorFormat;
    size_t mSize;
    int64_t mNumFramesOutput;
    FILE* mFile;

    YuvSource(const YuvSource &);
    YuvSource &operator=(const YuvSource &);
};

}  // namespace android

#endif // YUV_SOURCE_H_
