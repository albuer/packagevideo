#include "AvcSource.h"

#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>

extern int32_t gNumFramesOutput;
namespace android {

const uint8_t startCode[] = {0,0,0,1};
#define START_CODE_BYTES    (sizeof(startCode))

status_t getNextNALUnit(
        const uint8_t **_data, size_t *_size,
        const uint8_t **nalStart, size_t *nalSize,
        bool startCodeFollows) {
    const uint8_t *data = *_data;
    size_t size = *_size;

    *nalStart = NULL;
    *nalSize = 0;

    if (size < 3) {
        return -EAGAIN;
    }

    size_t offset = 0;

    // A valid startcode consists of at least two 0x00 bytes followed by 0x01.
    for (; offset + 2 < size; ++offset) {
        if (data[offset + 2] == 0x01 && data[offset] == 0x00
                && data[offset + 1] == 0x00) {
            break;
        }
    }
    if (offset + 2 >= size) {
        *_data = &data[offset];
        *_size = 2;
        return -EAGAIN;
    }
    offset += 3;

    size_t startOffset = offset;

    for (;;) {
        while (offset < size && data[offset] != 0x01) {
            ++offset;
        }

        if (offset == size) {
            if (startCodeFollows) {
                offset = size + 2;
                break;
            }

            return -EAGAIN;
        }

        if (data[offset - 1] == 0x00 && data[offset - 2] == 0x00) {
            break;
        }

        ++offset;
    }

    size_t endOffset = offset - 2;
    while (endOffset > startOffset + 1 && data[endOffset - 1] == 0x00) {
        --endOffset;
    }

    *nalStart = &data[startOffset];
    *nalSize = endOffset - startOffset;

    if (offset + 2 < size) {
        *_data = &data[offset - 2];
        *_size = size - offset + 2;
    } else {
        *_data = NULL;
        *_size = 0;
    }

    return OK;
}

AvcSource::AvcSource(int width, int height, int nFrames, int fps, int colorFormat, const char* filename)
    : mWidth(width),
      mHeight(height),
      mMaxNumFrames(nFrames),
      mFrameRate(fps),
      mColorFormat(colorFormat),
      mSize(width * height),
      mSawSpsPpsFrame(false),
      mSpsFrame(NULL), 
      mPpsFrame(NULL) {

    mGroup.add_buffer(new MediaBuffer(width * height));
    if (filename != NULL) {
        mFile = fopen(filename, "rb");
        mData = new uint8_t[mSize];
        mNalSize = fread(mData, 1, mSize, mFile);
        mNalData = mData;
    }
}

AvcSource::~AvcSource() {
    delete mData;
    if (mFile != NULL) {
        fclose(mFile);
    }
}

sp<MetaData> AvcSource::getFormat() {
    sp<MetaData> meta = new MetaData;
    meta->setInt32(kKeyWidth, mWidth);
    meta->setInt32(kKeyHeight, mHeight);
    meta->setInt32(kKeyColorFormat, mColorFormat);
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);

    return meta;
}

status_t AvcSource::start(MetaData *params __unused) {
    mNumFramesOutput = 0;
    gNumFramesOutput = 0;
    mSawSpsPpsFrame = false;
    mSpsFrame = NULL;
    mPpsFrame = NULL;
    return OK;
}

status_t AvcSource::stop() {
    gNumFramesOutput = mNumFramesOutput;
    if (mSpsFrame)
        delete mSpsFrame;
    if (mPpsFrame)
        delete mPpsFrame;
    return OK;
}

status_t AvcSource::read(
        MediaBuffer **buffer, const MediaSource::ReadOptions *options __unused) {

    if (mNumFramesOutput % 10 == 0) {
        fprintf(stderr, ".");
    }
    if (mNumFramesOutput == mMaxNumFrames) {
        printf("mMaxNumFrames: %d\n", mMaxNumFrames);
        return ERROR_END_OF_STREAM;
    }

    status_t err = mGroup.acquire_buffer(buffer);
    if (err != OK) {
        printf("acquire_buffer: %d\n", err);
        return err;
    }

    while(true) {
        const uint8_t *nalStart;
        size_t nalSize;
        if (getNALUnit(&nalStart, &nalSize) != OK) {
            printf("end of stream\n");
            (*buffer)->release();
            *buffer = NULL;
            return ERROR_END_OF_STREAM;
        }

        uint8_t nalType = nalStart[0] & 0x1F;
//        printf("Nal Type: 0x%02x\n", nalType);
        if (!mSawSpsPpsFrame && (nalType == 7 || nalType == 8)) {
            if (mSpsFrame == NULL && nalType == 7) {
                mSpsFrame = new uint8_t[nalSize+START_CODE_BYTES];
                memcpy(mSpsFrame, startCode, START_CODE_BYTES);
                memcpy(mSpsFrame+START_CODE_BYTES, nalStart, nalSize);
                mSpsFrameSize = nalSize+START_CODE_BYTES;
//                printf("get SPS %zu\n", mSpsFrameSize);
            } else if (mPpsFrame == NULL && nalType == 8) {
                mPpsFrame = new uint8_t[nalSize+START_CODE_BYTES];
                memcpy(mPpsFrame, startCode, START_CODE_BYTES);
                memcpy(mPpsFrame+START_CODE_BYTES, nalStart, nalSize);
                mPpsFrameSize = nalSize+START_CODE_BYTES;
//                printf("get PPS %zu\n", mPpsFrameSize);
            }

            if (mSpsFrame && mPpsFrame) {
                uint8_t * data = (uint8_t *)(*buffer)->data();
                memcpy(data, mSpsFrame, mSpsFrameSize);
                memcpy(data+mSpsFrameSize, mPpsFrame, mPpsFrameSize);
                (*buffer)->set_range(0, mSpsFrameSize+mPpsFrameSize);
                (*buffer)->meta_data()->clear();
                (*buffer)->meta_data()->setInt32(kKeyIsCodecConfig, true);
                mSawSpsPpsFrame = true;
                delete mSpsFrame;
                mSpsFrame = NULL;
                delete mPpsFrame;
                mPpsFrame = NULL;
//                printf("SPS PPS Frame\n");
                break;
            }
        } else {
            uint8_t * data = (uint8_t *)(*buffer)->data();
            memcpy(data, startCode, START_CODE_BYTES);
            memcpy(data+START_CODE_BYTES, nalStart, nalSize);
            (*buffer)->set_range(0, nalSize+START_CODE_BYTES);
            (*buffer)->meta_data()->clear();
            (*buffer)->meta_data()->setInt32(kKeyIsSyncFrame, nalType==5);
            (*buffer)->meta_data()->setInt64(
                    kKeyTime, (mNumFramesOutput * 1000000) / mFrameRate);
            (*buffer)->meta_data()->setInt64(
                    kKeyDecodingTime, (mNumFramesOutput * 1000000) / mFrameRate);
            ++mNumFramesOutput;
//            printf("%s Frame\n", nalType==5?"KEY":"NORMAL");
            break;
        }
    }

    return OK;
}

status_t AvcSource::getNALUnit(const uint8_t **nalStart, size_t *nalSize)
{
    while (getNextNALUnit(&mNalData, &mNalSize, nalStart, nalSize, false) != OK) {
        // read from file
        size_t remindSize = mSize - mNalSize;
        memcpy(mData, mNalData, mNalSize);
        mNalData = mData;
        size_t readSize = fread(mData+mNalSize, 1, remindSize, mFile);
        if (readSize <= 0)
            break;

        mNalSize += readSize;
    }
    if (*nalSize <= 0 &&
        getNextNALUnit(&mNalData, &mNalSize, nalStart, nalSize, true) != OK) {
        return ERROR_END_OF_STREAM;
    }
//    printf("NAL size=%zu\n", *nalSize);

    return OK;
}

}  // namespace android
