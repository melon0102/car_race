// mss_audio.cpp — real Miles Sound System replacement on AAudio.
// (Supersedes mss_stub.cpp.)
//
// The game hands whole WAV file images to AIL_set_sample_file and drives
// volume/pan/rate/loop per voice; this implements a small software mixer
// (32 voices + 1 music stream voice) feeding one AAudio output stream.
// Redbook (CD audio) stays silent — music tracks come back in Phase 4.

#define RV_NO_LONG32   // this TU includes system headers (AAudio, pthread)
#include "winshim/mss.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <pthread.h>
#include <math.h>

#define LOG_TAG "revolt-audio"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr int kMaxVoices = 33;      // 32 sfx + 1 stream
constexpr int kOutRate = 44100;

enum VoiceState { V_FREE, V_INIT, V_PLAYING, V_STOPPED, V_DONE };

struct Voice {
    VoiceState state = V_FREE;
    bool allocated = false;

    // source data (points into the game's file image — game owns the memory)
    const BYTE *data = nullptr;
    uint32_t frames = 0;
    int srcRate = 22050;
    bool is16 = true;
    bool stereo = false;

    void *ownedImage = nullptr;     // set for streams (we loaded the file)

    // playback
    double pos = 0.0;
    int rate = 22050;               // current playback rate (hz)
    int volume = 127;               // 0..127
    int pan = 64;                   // 0 left .. 127 right
    int loopsLeft = 1;              // -1 = infinite
};

Voice gVoices[kMaxVoices];
pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
AAudioStream *gStream = nullptr;
int gDeviceRate = kOutRate;

// parse a RIFF/WAVE PCM image; returns false if not playable
bool ParseWav(const void *image, Voice *v)
{
    const BYTE *p = (const BYTE *)image;
    if (memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) return false;
    uint32_t riffSize = *(const uint32_t *)(p + 4);

    const BYTE *end = p + 8 + riffSize;
    const BYTE *c = p + 12;
    int channels = 1, bits = 16, rate = 22050;
    const BYTE *dataPtr = nullptr;
    uint32_t dataLen = 0;

    while (c + 8 <= end) {
        uint32_t len = *(const uint32_t *)(c + 4);
        if (memcmp(c, "fmt ", 4) == 0 && len >= 16) {
            uint16_t fmt = *(const uint16_t *)(c + 8);
            channels = *(const uint16_t *)(c + 10);
            rate = (int)*(const uint32_t *)(c + 12);
            bits = *(const uint16_t *)(c + 22);
            if (fmt != 1 || (bits != 8 && bits != 16) || channels > 2) return false;
        } else if (memcmp(c, "data", 4) == 0) {
            dataPtr = c + 8;
            dataLen = len;
        }
        c += 8 + ((len + 1) & ~1u);
    }
    if (!dataPtr || !dataLen) return false;

    v->data = dataPtr;
    v->is16 = bits == 16;
    v->stereo = channels == 2;
    v->srcRate = v->rate = rate;
    v->frames = dataLen / (uint32_t)((bits / 8) * channels);
    v->pos = 0.0;
    return true;
}

// audio callback: mix all playing voices into int16 stereo
aaudio_data_callback_result_t MixCallback(AAudioStream *, void *,
                                          void *audioData, int32_t numFrames)
{
    int16_t *out = (int16_t *)audioData;
    memset(out, 0, (size_t)numFrames * 4);

    pthread_mutex_lock(&gLock);
    for (Voice &v : gVoices) {
        if (v.state != V_PLAYING || !v.data || !v.frames) continue;

        float vol = (float)v.volume / 127.0f;
        float panR = (float)v.pan / 127.0f;
        float gainL = vol * (1.0f - panR) * 2.0f;
        float gainR = vol * panR * 2.0f;
        if (gainL > 1.0f) gainL = 1.0f;
        if (gainR > 1.0f) gainR = 1.0f;
        double step = (double)v.rate / (double)gDeviceRate;

        for (int i = 0; i < numFrames; i++) {
            uint32_t idx = (uint32_t)v.pos;
            if (idx >= v.frames) {
                if (v.loopsLeft < 0 || --v.loopsLeft > 0) {
                    v.pos -= (double)v.frames;
                    idx = (uint32_t)v.pos;
                    if (idx >= v.frames) idx = 0;
                } else {
                    v.state = V_DONE;
                    break;
                }
            }

            float l, r;
            if (v.is16) {
                const int16_t *s = (const int16_t *)v.data;
                if (v.stereo) { l = s[idx * 2]; r = s[idx * 2 + 1]; }
                else l = r = s[idx];
            } else {
                const uint8_t *s = (const uint8_t *)v.data;
                if (v.stereo) {
                    l = ((int)s[idx * 2] - 128) << 8;
                    r = ((int)s[idx * 2 + 1] - 128) << 8;
                } else l = r = (float)(((int)s[idx] - 128) << 8);
            }

            int32_t ml = out[i * 2] + (int32_t)(l * gainL);
            int32_t mr = out[i * 2 + 1] + (int32_t)(r * gainR);
            if (ml > 32767) ml = 32767; else if (ml < -32768) ml = -32768;
            if (mr > 32767) mr = 32767; else if (mr < -32768) mr = -32768;
            out[i * 2] = (int16_t)ml;
            out[i * 2 + 1] = (int16_t)mr;

            v.pos += step;
        }
    }
    pthread_mutex_unlock(&gLock);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool StartStream()
{
    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, kOutRate);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setDataCallback(builder, MixCallback, nullptr);

    aaudio_result_t r = AAudioStreamBuilder_openStream(builder, &gStream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK || !gStream) {
        ALOGE("AAudio open failed: %d", r);
        return false;
    }
    gDeviceRate = AAudioStream_getSampleRate(gStream);
    if (gDeviceRate <= 0) gDeviceRate = kOutRate;
    AAudioStream_requestStart(gStream);
    ALOGI("AAudio stream started at %d Hz", gDeviceRate);
    return true;
}

Voice *V(HSAMPLE s) { return (Voice *)s; }
Voice *V(HSTREAM s) { return (Voice *)s; }

}  // namespace

// ---------------------------------------------------------------- Miles API

extern "C" {

S32 AIL_startup(void) { return 1; }

void AIL_shutdown(void)
{
    if (gStream) {
        AAudioStream_requestStop(gStream);
        AAudioStream_close(gStream);
        gStream = nullptr;
    }
}

void AIL_set_preference(U32, S32) {}

S32 AIL_waveOutOpen(HDIGDRIVER *drvr, void *, S32, WAVEFORMAT *)
{
    if (drvr) *drvr = (HDIGDRIVER)(intptr_t)1;
    return StartStream() ? 0 : -1;
}

// ---- samples ----

HSAMPLE AIL_allocate_sample_handle(HDIGDRIVER)
{
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < kMaxVoices - 1; i++) {   // last voice reserved for stream
        if (!gVoices[i].allocated) {
            gVoices[i] = Voice();
            gVoices[i].allocated = true;
            gVoices[i].state = V_INIT;
            pthread_mutex_unlock(&gLock);
            return (HSAMPLE)&gVoices[i];
        }
    }
    pthread_mutex_unlock(&gLock);
    return nullptr;
}

void AIL_release_sample_handle(HSAMPLE s)
{
    if (!s) return;
    pthread_mutex_lock(&gLock);
    *V(s) = Voice();
    pthread_mutex_unlock(&gLock);
}

void AIL_init_sample(HSAMPLE s)
{
    if (!s) return;
    pthread_mutex_lock(&gLock);
    Voice *v = V(s);
    v->state = V_INIT;
    v->data = nullptr;
    v->pos = 0.0;
    v->volume = 127;
    v->pan = 64;
    v->loopsLeft = 1;
    pthread_mutex_unlock(&gLock);
}

S32 AIL_set_sample_file(HSAMPLE s, const void *image, S32)
{
    if (!s || !image) return 0;
    pthread_mutex_lock(&gLock);
    S32 ok = ParseWav(image, V(s)) ? 1 : 0;
    pthread_mutex_unlock(&gLock);
    return ok;
}

void AIL_set_sample_loop_count(HSAMPLE s, S32 loops)
{
    if (!s) return;
    V(s)->loopsLeft = (loops == 0) ? -1 : loops;   // Miles: 0 = loop forever
}

void AIL_set_sample_pan(HSAMPLE s, S32 pan)
{
    if (s) V(s)->pan = pan < 0 ? 0 : (pan > 127 ? 127 : pan);
}

void AIL_set_sample_playback_rate(HSAMPLE s, S32 hz)
{
    if (s && hz > 0) V(s)->rate = hz;
}

void AIL_set_sample_volume(HSAMPLE s, S32 volume)
{
    if (s) V(s)->volume = volume < 0 ? 0 : (volume > 127 ? 127 : volume);
}

void AIL_start_sample(HSAMPLE s)
{
    if (!s) return;
    pthread_mutex_lock(&gLock);
    Voice *v = V(s);
    v->pos = 0.0;
    if (v->data) v->state = V_PLAYING;
    pthread_mutex_unlock(&gLock);
}

void AIL_stop_sample(HSAMPLE s)
{
    if (s && V(s)->state == V_PLAYING) V(s)->state = V_STOPPED;
}

void AIL_resume_sample(HSAMPLE s)
{
    if (s && V(s)->state == V_STOPPED) V(s)->state = V_PLAYING;
}

void AIL_end_sample(HSAMPLE s)
{
    if (s) V(s)->state = V_DONE;
}

U32 AIL_sample_status(HSAMPLE s)
{
    if (!s) return SMP_FREE;
    switch (V(s)->state) {
        case V_PLAYING: return SMP_PLAYING;
        case V_STOPPED: return SMP_STOPPED;
        case V_DONE:    return SMP_DONE;
        default:        return SMP_DONE;
    }
}

// ---- streams (music) — implemented as a preloaded voice ----

HSTREAM AIL_open_stream(HDIGDRIVER, const char *filename, S32)
{
    U32 size = AIL_file_size(filename);
    if (!size) return nullptr;
    void *image = AIL_file_read(filename, nullptr);
    if (!image) return nullptr;

    pthread_mutex_lock(&gLock);
    Voice *v = &gVoices[kMaxVoices - 1];
    *v = Voice();
    v->allocated = true;
    v->state = V_INIT;
    if (!ParseWav(image, v)) {
        v->allocated = false;
        pthread_mutex_unlock(&gLock);
        free(image);
        return nullptr;
    }
    v->ownedImage = image;
    pthread_mutex_unlock(&gLock);
    return (HSTREAM)v;
}

void AIL_close_stream(HSTREAM s)
{
    if (!s) return;
    pthread_mutex_lock(&gLock);
    Voice *v = V(s);
    void *image = v->ownedImage;
    *v = Voice();
    pthread_mutex_unlock(&gLock);
    free(image);
}

void AIL_start_stream(HSTREAM s)
{
    if (!s) return;
    pthread_mutex_lock(&gLock);
    Voice *v = V(s);
    v->pos = 0.0;
    if (v->data) v->state = V_PLAYING;
    pthread_mutex_unlock(&gLock);
}

void AIL_set_stream_loop_count(HSTREAM s, S32 count)
{
    if (s) V(s)->loopsLeft = (count == 0) ? -1 : count;
}

void AIL_set_stream_volume(HSTREAM s, S32 volume)
{
    if (s) V(s)->volume = volume < 0 ? 0 : (volume > 127 ? 127 : volume);
}

void AIL_set_stream_playback_rate(HSTREAM s, S32 rate)
{
    if (s && rate > 0) V(s)->rate = rate;
}

// ---- redbook (CD audio) — silent on Android ----

HREDBOOK AIL_redbook_open(U32) { return nullptr; }
void AIL_redbook_play(HREDBOOK, U32) {}
void AIL_redbook_track_info(HREDBOOK, U32, U32 *start, U32 *end)
{
    if (start) *start = 0;
    if (end) *end = 0;
}

// ---- file utils ----

U32 AIL_file_size(const char *filename)
{
    FILE *fp = fopen(filename, "rb");   // -> rv_fopen via windows.h
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return (U32)size;
}

void *AIL_file_read(const char *filename, void *dest)
{
    U32 size = AIL_file_size(filename);
    if (!size) return nullptr;
    void *buf = (dest && dest != FILE_READ_WITH_SIZE) ? dest : malloc(size);
    if (!buf) return nullptr;
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        if (buf != dest) free(buf);
        return nullptr;
    }
    fread(buf, 1, size, fp);
    fclose(fp);
    return buf;
}

void AIL_mem_free_lock(void *ptr) { free(ptr); }

}  // extern "C"
