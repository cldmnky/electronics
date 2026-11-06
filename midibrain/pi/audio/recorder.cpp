#include "recorder.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>

namespace mnky::audio {
namespace {

constexpr uint16_t kFormatIeeeFloat = 3;
constexpr uint32_t kFmtChunkBytes = 16;
constexpr uint32_t kFactChunkBytes = 4;
constexpr uint32_t kBitsPerSample = 32;
// 4 (RIFF) + 4 (size) + 4 (WAVE) + 8 + 16 (fmt) + 8 + 4 (fact) + 8 (data).
constexpr uint32_t kRiffBytesBeforeData = 48;

void putLe32(std::FILE* file, uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu),
    };
    std::fwrite(bytes, 1, sizeof bytes, file);
}

void putLe16(std::FILE* file, uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
    };
    std::fwrite(bytes, 1, sizeof bytes, file);
}

std::string utcStamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::tm* utc = std::gmtime(&now);
    char buffer[32] = {};
    if (utc == nullptr) return "19700101T000000Z";
    if (std::strftime(buffer, sizeof buffer, "%Y%m%dT%H%M%SZ", utc) == 0) return "19700101T000000Z";
    return buffer;
}

std::string channelFileName(int index) {
    std::string name = index < 10 ? "ch0" : "ch";
    name += std::to_string(index);
    name += ".wav";
    return name;
}

bool armed(uint64_t mask, int channel) {
    return channel >= 0 && channel < kMaxTracks &&
           (mask & (uint64_t{1} << static_cast<unsigned>(channel))) != 0;
}

}  // namespace

WavFileWriter::~WavFileWriter() {
    if (mFile != nullptr) close();
}

bool WavFileWriter::open(const std::string& path, int channels, uint32_t sampleRate) {
    if (mFile != nullptr || channels < 1 || sampleRate == 0) return false;
    std::FILE* file = std::fopen(path.c_str(), "wb+");
    if (file == nullptr) return false;

    mFile = file;
    mChannels = channels;
    mFrames = 0;

    std::fwrite("RIFF", 1, 4, file);
    putLe32(file, kRiffBytesBeforeData);
    std::fwrite("WAVE", 1, 4, file);
    std::fwrite("fmt ", 1, 4, file);
    putLe32(file, kFmtChunkBytes);
    putLe16(file, kFormatIeeeFloat);
    putLe16(file, static_cast<uint16_t>(channels));
    putLe32(file, sampleRate);
    putLe32(file, sampleRate * static_cast<uint32_t>(channels) * (kBitsPerSample / 8u));
    putLe16(file, static_cast<uint16_t>(channels) * (kBitsPerSample / 8u));
    putLe16(file, kBitsPerSample);
    std::fwrite("fact", 1, 4, file);
    putLe32(file, kFactChunkBytes);
    putLe32(file, 0);
    std::fwrite("data", 1, 4, file);
    putLe32(file, 0);

    if (std::ftell(file) != kHeaderBytes) {
        std::fclose(file);
        mFile = nullptr;
        mChannels = 0;
        return false;
    }
    return true;
}

bool WavFileWriter::writeFrames(const float* interleaved, int frames) {
    if (mFile == nullptr || interleaved == nullptr || frames <= 0) return false;
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(mChannels);
    if (std::fwrite(interleaved, sizeof(float), samples, mFile) != samples) return false;
    mFrames += static_cast<uint64_t>(frames);
    return true;
}

bool WavFileWriter::close() {
    if (mFile == nullptr) return false;
    const uint64_t dataBytes = mFrames * static_cast<uint64_t>(mChannels) * (kBitsPerSample / 8u);
    // RIFF sizes are 32-bit; a take longer than 4 GiB per file cannot be described.
    const bool fits = dataBytes <= 0xffffffffull;

    std::fseek(mFile, kFactFramesOffset, SEEK_SET);
    putLe32(mFile, static_cast<uint32_t>(mFrames));
    std::fseek(mFile, kDataBytesOffset, SEEK_SET);
    putLe32(mFile, static_cast<uint32_t>(dataBytes));
    std::fseek(mFile, 4, SEEK_SET);
    putLe32(mFile, static_cast<uint32_t>(kRiffBytesBeforeData + dataBytes));

    const bool closed = std::fclose(mFile) == 0;
    mFile = nullptr;
    return closed && fits;
}

Recorder::Recorder() : mQueue(static_cast<size_t>(kQueueBlocks)) {}

Recorder::~Recorder() {
    if (recording()) stopTake();
    if (mThread.joinable()) mThread.join();
}

bool Recorder::startTake(const std::string& dir, uint64_t armMask, uint32_t sampleRate,
                         bool masterArm) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mRunning.load(std::memory_order_acquire)) return false;
    if (dir.empty() || sampleRate == 0) return false;
    // One bit per track; the mask is the full 64-bit track mask. A capacity
    // above 64 would need a wider type, so fail loudly rather than silently
    // dropping tracks.
    static_assert(kMaxTracks <= 64, "arm mask is one uint64_t");
    const uint64_t mask = armMask;
    if (mask == 0) return false;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    const std::string base = (std::filesystem::path(dir) / ("take-" + utcStamp())).string();
    std::string takeDir;
    for (int attempt = 0; attempt < 1000 && takeDir.empty(); ++attempt) {
        const std::string candidate = attempt == 0 ? base : base + "-" + std::to_string(attempt);
        std::error_code mkdirEc;
        if (std::filesystem::create_directory(candidate, mkdirEc)) {
            takeDir = candidate;
        } else if (mkdirEc) {
            return false;
        }
    }
    if (takeDir.empty()) return false;

    bool opened = true;
    for (int c = 0; c < kMaxTracks && opened; ++c) {
        if (!armed(mask, c)) continue;
        opened = mChannelFile[c].open((std::filesystem::path(takeDir) / channelFileName(c)).string(), 1, sampleRate);
    }
    opened = opened &&
             (!masterArm ||
              mMasterFile.open((std::filesystem::path(takeDir) / "master.wav").string(), 2, sampleRate));
    if (!opened) {
        for (int c = 0; c < kMaxTracks; ++c) {
            if (armed(mask, c)) mChannelFile[c].close();
        }
        mMasterFile.close();
        return false;
    }

    mArmMask = mask;
    mMasterArm = masterArm;
    mTakeDir = takeDir;
    mHead = 0;
    mTail = 0;
    mCount = 0;
    mStop = false;
    mDrops.store(0, std::memory_order_relaxed);
    mFrames.store(0, std::memory_order_relaxed);
    mRunning.store(true, std::memory_order_release);
    mThread = std::thread(&Recorder::writerLoop, this);
    return true;
}

void Recorder::writeBlock(const float* const* taps, const float* masterL, const float* masterR, int n) {
    if (masterL == nullptr || masterR == nullptr || n <= 0) return;
    const int frames = n < kBlockSize ? n : kBlockSize;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mRunning.load(std::memory_order_acquire) || mStop) return;
        if (mCount == kQueueBlocks) {
            mDrops.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Block& block = mQueue[static_cast<size_t>(mHead)];
        block.n = frames;
        for (int i = 0; i < frames; ++i) {
            block.master[0][i] = masterL[i];
            block.master[1][i] = masterR[i];
        }
        for (int c = 0; c < kMaxTracks; ++c) {
            if (!armed(mArmMask, c)) continue;
            const float* src = taps != nullptr ? taps[c] : nullptr;
            float* dst = block.ch[c];
            for (int i = 0; i < frames; ++i) dst[i] = src != nullptr ? src[i] : 0.0f;
        }
        mHead = (mHead + 1) % kQueueBlocks;
        ++mCount;
    }
    mCv.notify_one();
}

Recorder::Take Recorder::stopTake() {
    Take take;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mRunning.load(std::memory_order_acquire)) return take;
        mStop = true;
        mRunning.store(false, std::memory_order_release);
    }
    mCv.notify_all();
    if (mThread.joinable()) mThread.join();

    int files = 0;
    for (int c = 0; c < kMaxTracks; ++c) {
        if (armed(mArmMask, c) && mChannelFile[c].close()) ++files;
    }
    if (mMasterArm && mMasterFile.close()) ++files;

    take.path = mTakeDir;
    take.files = files;
    take.frames = mFrames.load(std::memory_order_relaxed);
    take.drops = mDrops.load(std::memory_order_relaxed);
    mTakeDir.clear();
    return take;
}

void Recorder::writerLoop() {
    std::unique_lock<std::mutex> lock(mMutex);
    for (;;) {
        mCv.wait(lock, [this] { return mCount > 0 || mStop; });
        if (mCount == 0) break;
        mScratch = mQueue[static_cast<size_t>(mTail)];
        mTail = (mTail + 1) % kQueueBlocks;
        --mCount;
        lock.unlock();
        writeBlockToDisk(mScratch);
        lock.lock();
    }
}

void Recorder::writeBlockToDisk(const Block& block) {
    const int frames = block.n;
    for (int c = 0; c < kMaxTracks; ++c) {
        if (!armed(mArmMask, c)) continue;
        // A write failure is surfaced through the take's file data size, not drops.
        (void)mChannelFile[c].writeFrames(block.ch[c], frames);
    }
    float interleaved[2 * kBlockSize];
    bool wroteMaster = true;
    if (mMasterArm) {
        for (int i = 0; i < frames; ++i) {
            interleaved[2 * i] = block.master[0][i];
            interleaved[2 * i + 1] = block.master[1][i];
        }
        wroteMaster = mMasterFile.writeFrames(interleaved, frames);
    }
    if (wroteMaster) {
        mFrames.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);
    }
}

}  // namespace mnky::audio
