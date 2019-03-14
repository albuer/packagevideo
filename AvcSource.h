#ifndef AVC_SOURCE_H_

#define AVC_SOURCE_H_

#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <utils/Compat.h>

namespace android {

class AvcSource : public MediaSource {

public:
    AvcSource(int width, int height, int nFrames, int fps, int colorFormat, const char* filename);

    virtual sp<MetaData> getFormat();
    virtual status_t start(MetaData *params __unused);
    virtual status_t stop();
    virtual status_t read(MediaBuffer **buffer, const MediaSource::ReadOptions *options __unused);
    status_t getNALUnit(const uint8_t **nalStart, size_t *nalSize);

protected:
    virtual ~AvcSource();

private:
    MediaBufferGroup mGroup;
    int mWidth, mHeight;
    int mMaxNumFrames;
    int mFrameRate;
    int mColorFormat;
    int64_t mNumFramesOutput;
    FILE* mFile;
    const uint8_t* mNalData;
    size_t mNalSize;
    uint8_t* mData;
    size_t mSize;
    bool mSawSpsPpsFrame;
    uint8_t* mSpsFrame;
    size_t mSpsFrameSize;
    uint8_t* mPpsFrame;
    size_t mPpsFrameSize;

    AvcSource(const AvcSource &);
    AvcSource &operator=(const AvcSource &);
};

}  // namespace android

#endif // AVC_SOURCE_H_