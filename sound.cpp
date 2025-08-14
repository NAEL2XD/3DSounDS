// sound.cpp
#include "sound.hpp"

typedef struct {
    ndspWaveBuf waveBufs[3];
    OggVorbis_File ogg;
    int16_t* audioBuffer = nullptr;
    int channel;
    Thread threadID;
    FILE* file;
    bool active;
    bool quit;
    bool* loop;
    int* time;
} CH;

CH storedChan[24];
LightEvent s_event;

size_t i16 = sizeof(int16_t);

bool audioInit(CH* channel) {
    vorbis_info* vi = ov_info(&channel->ogg, -1);

    ndspChnReset(channel->channel);
    ndspChnSetInterp(channel->channel, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(channel->channel, vi->rate);
    ndspChnSetFormat(channel->channel, vi->channels == 1 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetMix(channel->channel, (float[12]){1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    size_t SAMPLES_PER_BUF = vi->rate * 28 / 1000;
    size_t CHANNELS_PER_SAMPLE = vi->channels;
    size_t bufferSize = (SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE * i16) * 3;

    channel->audioBuffer = (int16_t*)linearAlloc(bufferSize);
    if (!channel->audioBuffer) return false;

    memset(&channel->waveBufs, 0, sizeof(channel->waveBufs));
    int16_t* buffer = channel->audioBuffer;

    for (size_t i = 0; i < 3; i++) {
        channel->waveBufs[i].data_vaddr = buffer;
        channel->waveBufs[i].nsamples = SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE;
        channel->waveBufs[i].status = NDSP_WBUF_DONE;
        buffer += SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE;
    }

    return true;
}

bool fillBuffer(CH* channel) {
    for (size_t i = 0; i < 3; ++i) {
        if (channel->waveBufs[i].status != NDSP_WBUF_DONE) continue;

        int totalBytes = 0;
        while ((size_t)totalBytes < channel->waveBufs[i].nsamples * i16) {
            int16_t* buffer = (int16_t*)((uint8_t*)channel->waveBufs[i].data_vaddr + totalBytes);
            size_t bufferSize = (channel->waveBufs[i].nsamples * i16 - totalBytes);

            int bytesRead = ov_read(&channel->ogg, (char*)buffer, bufferSize, nullptr);
            if (bytesRead <= 0) {
                if (bytesRead == 0) {
                    if (channel->loop) {
                        ov_time_seek(&channel->ogg, 0);
                        bytesRead = 0;
                    }
                    break;
                }
                printf("ov_read error: %d\n", bytesRead);
                break;
            }
            totalBytes += bytesRead;
        }

        if (totalBytes == 0) {
            return false;
        }

        channel->waveBufs[i].nsamples = totalBytes / i16;
        DSP_FlushDataCache(channel->waveBufs[i].data_vaddr, totalBytes);
        ndspChnWaveBufAdd(channel->channel, &channel->waveBufs[i]);
    }
    return true;
}

void audioThread(void* arg) {
    CH* channel = (CH*)arg;
    while (!channel->quit) {
        if (!fillBuffer(channel)) break;
        LightEvent_Wait(&s_event);
        *channel->time = static_cast<int>(ov_time_tell(&channel->ogg));
    }

    // Finish!
    channel->active = false;
}

Sound::Sound(std::string path):
    channel(-1),
    length(0),
    time(0),
    loop(false),
    soundPath("romfs:/" + path)
{
    LightEvent_Init(&s_event, RESET_ONESHOT);
    
    for (int i = 0; i < 24; i++) {
        if (!storedChan[i].active) {
            channel = i;
            break;
        }
    }

    if (channel == -1) {
        printf("No free channels found!?\n");
        return;
    }

    storedChan[channel].channel = channel;
    storedChan[channel].file = fopen(soundPath.c_str(), "rb");
    storedChan[channel].time = &time;
    storedChan[channel].quit = false;
    storedChan[channel].loop = &loop;
    
    if (!storedChan[channel].file) {
        printf("Failed to open file: %s\n", soundPath.c_str());
        return;
    }
    
    if (ov_open(storedChan[channel].file, &storedChan[channel].ogg, nullptr, 0) != 0) {
        fclose(storedChan[channel].file);
        printf("Failed to open audio!?\n");
        return;
    }

    if (!audioInit(&storedChan[channel])) {
        fclose(storedChan[channel].file);
        ov_clear(&storedChan[channel].ogg);
        printf("Failed to initialize audio!?\n");
        return;
    }

    length = (int)ov_time_total(&storedChan[channel].ogg, -1);
}

void Sound::play(int ms) {
    CH* chlol = &storedChan[channel];
    if (channel == -1) {
        return;
    }

    if (chlol->active) {
        stop();
    }

    ov_time_seek(&chlol->ogg, ms);
    
    int32_t priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    priority -= 1;
    priority = priority < 0x18 ? 0x18 : priority;
    priority = priority > 0x3F ? 0x3F : priority;

    ndspSetCallback([](void*) {
        LightEvent_Signal(&s_event);
    }, nullptr);

    chlol->quit = false;
    chlol->threadID = threadCreate(audioThread, chlol, 32768, priority, -1, false);
    if (chlol->threadID) {
        chlol->active = true;
    }
}

void Sound::stop() {
    CH* chlol = &storedChan[channel];
    if (!chlol->active || channel == -1) return;
    
    chlol->quit = true;
    LightEvent_Signal(&s_event);
    
    if (chlol->threadID) {
        threadJoin(chlol->threadID, UINT64_MAX);
        threadFree(chlol->threadID);
        chlol->threadID = nullptr;
    }
    
    chlol->active = false;
}

Sound::~Sound() {
    stop();
    
    if (channel != -1) {
        CH* chlol = &storedChan[channel];
        chlol->active = false;
        if (chlol->audioBuffer) {
            linearFree(chlol->audioBuffer);
            chlol->audioBuffer = nullptr;
        }
        
        ov_clear(&chlol->ogg);
        if (chlol->file) {
            fclose(chlol->file);
            chlol->file = nullptr;
        }
        
        ndspChnReset(channel);
    }
}