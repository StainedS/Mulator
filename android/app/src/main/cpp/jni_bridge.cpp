// JNI bridge between the Kotlin UI and the M-ulator core.
//
// Threads:
//   * UI thread   — calls the JNI functions below.
//   * emu thread  — runs frames at ~59.73 Hz, draws into the Surface.
//   * audio thread (AAudio callback) — pulls samples from a lock-free ring.
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mulator/machine.h"

#define LOG_TAG "Mulator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace mulator;

namespace {

constexpr int kSampleRate = 48000;
constexpr size_t kMaxQueuedFrames = kSampleRate / 10; // keep audio latency under ~100 ms

// Single-producer / single-consumer ring of interleaved stereo frames.
class AudioRing {
public:
    explicit AudioRing(size_t frames) : buf_(frames * 2), cap_(frames) {}

    size_t queued() const { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }

    void write(const int16_t* data, size_t frames) {
        size_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
        frames = std::min(frames, cap_ - (w - r));
        for (size_t i = 0; i < frames; i++) {
            size_t slot = ((w + i) % cap_) * 2;
            buf_[slot] = data[i * 2];
            buf_[slot + 1] = data[i * 2 + 1];
        }
        w_.store(w + frames, std::memory_order_release);
    }

    size_t read(int16_t* out, size_t frames) {
        size_t r = r_.load(std::memory_order_relaxed), w = w_.load(std::memory_order_acquire);
        frames = std::min(frames, w - r);
        for (size_t i = 0; i < frames; i++) {
            size_t slot = ((r + i) % cap_) * 2;
            out[i * 2] = buf_[slot];
            out[i * 2 + 1] = buf_[slot + 1];
        }
        r_.store(r + frames, std::memory_order_release);
        return frames;
    }

private:
    std::vector<int16_t> buf_;
    size_t cap_;
    std::atomic<size_t> r_{0}, w_{0};
};

// History of recent save states for rewinding. The newest state is kept whole;
// older ones are stored as deflated XOR deltas against the next newer state,
// which are mostly zeros and compress to a few KB each.
class RewindBuffer {
public:
    static constexpr size_t kBudgetBytes = 48u << 20;

    void clear() {
        current_.clear();
        deltas_.clear();
        bytes_ = 0;
    }

    bool empty() const { return current_.empty(); }

    void push(std::vector<uint8_t>&& state) {
        if (current_.size() != state.size()) {
            clear();
            current_ = std::move(state);
            return;
        }
        for (size_t i = 0; i < state.size(); i++) current_[i] ^= state[i];
        std::vector<uint8_t> packed(compressBound(uLong(current_.size())));
        uLongf len = uLongf(packed.size());
        if (compress2(packed.data(), &len, current_.data(), uLong(current_.size()), 1) != Z_OK) {
            clear();
            current_ = std::move(state);
            return;
        }
        packed.resize(len);
        packed.shrink_to_fit();
        bytes_ += packed.size();
        deltas_.push_back(std::move(packed));
        while (bytes_ > kBudgetBytes && !deltas_.empty()) {
            bytes_ -= deltas_.front().size();
            deltas_.pop_front();
        }
        current_ = std::move(state);
    }

    // Removes and returns the newest state; the next older one becomes the newest.
    // The oldest state is never removed, so rewinding stops there.
    const std::vector<uint8_t>& stepBack() {
        returned_ = current_;
        if (deltas_.empty()) return returned_;
        std::vector<uint8_t> delta(current_.size());
        uLongf len = uLongf(delta.size());
        const auto& packed = deltas_.back();
        if (uncompress(delta.data(), &len, packed.data(), uLong(packed.size())) == Z_OK && len == delta.size()) {
            for (size_t i = 0; i < delta.size(); i++) current_[i] ^= delta[i];
        }
        bytes_ -= packed.size();
        deltas_.pop_back();
        return returned_;
    }

private:
    std::vector<uint8_t> current_;
    std::vector<uint8_t> returned_;
    std::deque<std::vector<uint8_t>> deltas_;
    size_t bytes_ = 0;
};

constexpr int kRewindInterval = 6;  // snapshot every 6 frames (10 per second)
constexpr int kRewindStepFrames = 2; // while rewinding, step back every 2nd frame (3x speed)

struct Emulator {
    Machine machine;
    std::mutex machineMutex; // guards machine between the emu thread and UI-thread calls
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> fastForward{false};
    std::atomic<int> fastForwardSpeed{3};
    std::atomic<bool> rewindEnabled{true};
    std::atomic<bool> rewinding{false};
    RewindBuffer rewind; // guarded by machineMutex
    std::atomic<uint16_t> keys{0};

    std::mutex windowMutex;
    ANativeWindow* window = nullptr;
    int scale = 1;

    AAudioStream* audio = nullptr;
    AudioRing ring{16384};

    std::string savePath;
    std::vector<uint32_t> frame = std::vector<uint32_t>(SCREEN_WIDTH * SCREEN_HEIGHT);
};

Emulator* gEmu = nullptr;

std::string jstr(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out(c);
    env->ReleaseStringUTFChars(s, c);
    return out;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

// Caller must hold machineMutex.
void flushSaveLocked(Emulator* e) {
    if (e->savePath.empty() || !e->machine.hasSaveMemory() || !e->machine.saveDirty()) return;
    if (writeFileAtomic(e->savePath, e->machine.saveData())) e->machine.clearSaveDirty();
    else LOGE("could not write save file %s", e->savePath.c_str());
}

// ---------------------------------------------------------------- audio
aaudio_data_callback_result_t audioCallback(AAudioStream*, void* user, void* data, int32_t frames) {
    auto* e = static_cast<Emulator*>(user);
    auto* out = static_cast<int16_t*>(data);
    size_t got = e->ring.read(out, size_t(frames));
    if (got < size_t(frames)) std::memset(out + got * 2, 0, (size_t(frames) - got) * 4);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void openAudio(Emulator* e) {
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, kSampleRate);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, audioCallback, e);
    if (AAudioStreamBuilder_openStream(b, &e->audio) != AAUDIO_OK) {
        LOGE("AAudio open failed; running without sound");
        e->audio = nullptr;
    }
    AAudioStreamBuilder_delete(b);
    if (!e->audio) return;
    int rate = AAudioStream_getSampleRate(e->audio);
    {
        std::lock_guard<std::mutex> lock(e->machineMutex);
        e->machine.setAudioSampleRate(rate > 0 ? rate : kSampleRate);
    }
    AAudioStream_requestStart(e->audio);
    LOGI("audio: %d Hz, burst %d frames", rate, AAudioStream_getFramesPerBurst(e->audio));
}

void closeAudio(Emulator* e) {
    if (!e->audio) return;
    AAudioStream_requestStop(e->audio);
    AAudioStream_close(e->audio);
    e->audio = nullptr;
}

// ---------------------------------------------------------------- video
void present(Emulator* e) {
    std::lock_guard<std::mutex> lock(e->windowMutex);
    if (!e->window) return;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(e->window, &buf, nullptr) != 0) return;
    int k = e->scale;
    if (buf.width < SCREEN_WIDTH * k || buf.height < SCREEN_HEIGHT * k) k = 1;
    if (buf.width >= SCREEN_WIDTH && buf.height >= SCREEN_HEIGHT) {
        auto* dst = static_cast<uint32_t*>(buf.bits);
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            const uint32_t* src = e->frame.data() + y * SCREEN_WIDTH;
            uint32_t* row = dst + size_t(y * k) * buf.stride;
            if (k == 1) {
                std::memcpy(row, src, SCREEN_WIDTH * 4);
                continue;
            }
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                uint32_t p = src[x];
                for (int i = 0; i < k; i++) row[x * k + i] = p;
            }
            for (int j = 1; j < k; j++)
                std::memcpy(dst + size_t(y * k + j) * buf.stride, row, size_t(SCREEN_WIDTH * k) * 4);
        }
    }
    ANativeWindow_unlockAndPost(e->window);
}

// ---------------------------------------------------------------- emulation thread
void runLoop(Emulator* e) {
    using clock = std::chrono::steady_clock;
    const auto frameTime = std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(1.0 / FRAMES_PER_SECOND));
    auto next = clock::now();
    int frames = 0;
    int sinceSnapshot = 0;
    std::vector<int16_t> tmp(4096 * 2);

    while (e->running.load()) {
        if (e->paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            next = clock::now();
            continue;
        }
        bool ff = e->fastForward.load();
        int speed = e->fastForwardSpeed.load(); // 0 = as fast as possible
        bool rw = e->rewinding.load() && e->rewindEnabled.load();
        {
            std::lock_guard<std::mutex> lock(e->machineMutex);
            e->machine.setKeys(e->keys.load());
            if (rw) {
                if (frames % kRewindStepFrames == 0 && !e->rewind.empty()) {
                    const auto& state = e->rewind.stepBack();
                    e->machine.loadState(state.data(), state.size());
                    e->machine.runFrame(); // render the restored moment
                }
                while (e->machine.readAudio(tmp.data(), 4096) > 0) {} // no sound while rewinding
            } else {
                int runs = !ff ? 1 : speed > 0 ? speed : 4;
                for (int i = 0; i < runs; i++) {
                    e->machine.runFrame();
                    if (e->rewindEnabled.load() && ++sinceSnapshot >= kRewindInterval) {
                        sinceSnapshot = 0;
                        e->rewind.push(e->machine.saveState());
                    }
                }
                size_t n;
                while ((n = e->machine.readAudio(tmp.data(), 4096)) > 0) {
                    if (!ff && e->ring.queued() < kMaxQueuedFrames) e->ring.write(tmp.data(), n);
                }
            }
            std::memcpy(e->frame.data(), e->machine.framebuffer(), e->frame.size() * 4);
            if (++frames % 300 == 0) flushSaveLocked(e); // roughly every 5 seconds
        }
        present(e);

        if (ff && speed == 0 && !rw) {
            next = clock::now();
        } else {
            next += frameTime;
            auto now = clock::now();
            if (next > now) std::this_thread::sleep_until(next);
            else if (now - next > frameTime * 6) next = now; // fell far behind: don't try to catch up
        }
    }
}

void stopThread(Emulator* e) {
    if (!e->running.exchange(false)) return;
    if (e->thread.joinable()) e->thread.join();
    closeAudio(e);
    std::lock_guard<std::mutex> lock(e->machineMutex);
    flushSaveLocked(e);
}

} // namespace

// ---------------------------------------------------------------- JNI exports
#define JNI_FN(name) Java_com_mulator_app_NativeBridge_##name

extern "C" {

JNIEXPORT jboolean JNICALL JNI_FN(loadRom)(JNIEnv* env, jclass, jbyteArray rom, jstring savePath) {
    if (!gEmu) gEmu = new Emulator();
    Emulator* e = gEmu;
    stopThread(e);
    jsize len = env->GetArrayLength(rom);
    std::vector<uint8_t> data(static_cast<size_t>(len));
    env->GetByteArrayRegion(rom, 0, len, reinterpret_cast<jbyte*>(data.data()));

    std::lock_guard<std::mutex> lock(e->machineMutex);
    if (!e->machine.loadRom(data.data(), data.size())) return JNI_FALSE;
    e->savePath = jstr(env, savePath);
    if (e->machine.hasSaveMemory()) {
        auto save = readFile(e->savePath);
        if (!save.empty()) e->machine.loadSaveData(save.data(), save.size());
    }
    e->rewind.clear();
    e->keys = 0;
    e->fastForward = false;
    e->rewinding = false;
    e->paused = false;
    LOGI("loaded \"%s\" [%s], save type %s", e->machine.romTitle().c_str(), e->machine.gameCode().c_str(),
         e->machine.saveTypeName().c_str());
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL JNI_FN(romTitle)(JNIEnv* env, jclass) {
    if (!gEmu) return env->NewStringUTF("");
    std::lock_guard<std::mutex> lock(gEmu->machineMutex);
    return env->NewStringUTF(gEmu->machine.romTitle().c_str());
}

JNIEXPORT void JNICALL JNI_FN(start)(JNIEnv*, jclass) {
    Emulator* e = gEmu;
    if (!e || e->running.load()) return;
    openAudio(e);
    e->running = true;
    e->thread = std::thread(runLoop, e);
}

JNIEXPORT void JNICALL JNI_FN(stop)(JNIEnv*, jclass) {
    if (gEmu) stopThread(gEmu);
}

JNIEXPORT void JNICALL JNI_FN(setPaused)(JNIEnv*, jclass, jboolean paused) {
    Emulator* e = gEmu;
    if (!e) return;
    e->paused = paused;
    if (paused) {
        if (e->audio) AAudioStream_requestPause(e->audio);
        std::lock_guard<std::mutex> lock(e->machineMutex);
        flushSaveLocked(e);
    } else if (e->audio) {
        AAudioStream_requestStart(e->audio);
    }
}

JNIEXPORT void JNICALL JNI_FN(setSurface)(JNIEnv* env, jclass, jobject surface, jint width, jint height) {
    Emulator* e = gEmu;
    if (!e) return;
    std::lock_guard<std::mutex> lock(e->windowMutex);
    if (e->window) {
        ANativeWindow_release(e->window);
        e->window = nullptr;
    }
    if (!surface) return;
    e->window = ANativeWindow_fromSurface(env, surface);
    if (!e->window) return;
    // Upscale with nearest-neighbour ourselves so pixels stay crisp; the
    // compositor only has to cover the small remainder.
    double fit = std::min(width / double(SCREEN_WIDTH), height / double(SCREEN_HEIGHT));
    e->scale = std::max(1, std::min(6, int(std::ceil(fit))));
    ANativeWindow_setBuffersGeometry(e->window, SCREEN_WIDTH * e->scale, SCREEN_HEIGHT * e->scale,
                                     WINDOW_FORMAT_RGBA_8888);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv*, jclass, jint keys) {
    if (gEmu) gEmu->keys = uint16_t(keys & 0x3FF);
}

JNIEXPORT void JNICALL JNI_FN(setFastForward)(JNIEnv*, jclass, jboolean on) {
    if (gEmu) gEmu->fastForward = on;
}

JNIEXPORT void JNICALL JNI_FN(reset)(JNIEnv*, jclass) {
    if (!gEmu) return;
    std::lock_guard<std::mutex> lock(gEmu->machineMutex);
    gEmu->machine.reset();
    gEmu->rewind.clear();
}

JNIEXPORT jboolean JNICALL JNI_FN(saveState)(JNIEnv* env, jclass, jstring path) {
    if (!gEmu) return JNI_FALSE;
    std::vector<uint8_t> state;
    {
        std::lock_guard<std::mutex> lock(gEmu->machineMutex);
        state = gEmu->machine.saveState();
    }
    return writeFileAtomic(jstr(env, path), state) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(loadState)(JNIEnv* env, jclass, jstring path) {
    if (!gEmu) return JNI_FALSE;
    auto state = readFile(jstr(env, path));
    if (state.empty()) return JNI_FALSE;
    std::lock_guard<std::mutex> lock(gEmu->machineMutex);
    if (!gEmu->machine.loadState(state.data(), state.size())) return JNI_FALSE;
    gEmu->rewind.clear();
    std::memcpy(gEmu->frame.data(), gEmu->machine.framebuffer(), gEmu->frame.size() * 4);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL JNI_FN(flushSave)(JNIEnv*, jclass) {
    if (!gEmu) return;
    std::lock_guard<std::mutex> lock(gEmu->machineMutex);
    flushSaveLocked(gEmu);
}

JNIEXPORT void JNICALL JNI_FN(setFastForwardSpeed)(JNIEnv*, jclass, jint speed) {
    if (gEmu) gEmu->fastForwardSpeed = std::max(0, std::min(16, int(speed)));
}

JNIEXPORT void JNICALL JNI_FN(setRewindEnabled)(JNIEnv*, jclass, jboolean on) {
    if (!gEmu) return;
    gEmu->rewindEnabled = on;
    if (!on) {
        std::lock_guard<std::mutex> lock(gEmu->machineMutex);
        gEmu->rewind.clear();
    }
}

JNIEXPORT void JNICALL JNI_FN(setRewinding)(JNIEnv*, jclass, jboolean on) {
    if (gEmu) gEmu->rewinding = on;
}

// The last presented frame as ARGB pixels (Android Bitmap layout), for thumbnails.
JNIEXPORT jintArray JNICALL JNI_FN(screenshot)(JNIEnv* env, jclass) {
    jintArray out = env->NewIntArray(SCREEN_WIDTH * SCREEN_HEIGHT);
    if (!gEmu || !out) return out;
    std::vector<jint> argb(SCREEN_WIDTH * SCREEN_HEIGHT);
    {
        std::lock_guard<std::mutex> lock(gEmu->machineMutex);
        for (size_t i = 0; i < argb.size(); i++) {
            uint32_t p = gEmu->frame[i]; // 0xAABBGGRR
            argb[i] = jint(0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu));
        }
    }
    env->SetIntArrayRegion(out, 0, jsize(argb.size()), argb.data());
    return out;
}

} // extern "C"
