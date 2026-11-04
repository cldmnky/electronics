#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine.hpp"

namespace mnky::audio {

/// Minimal RIFF/WAVE writer for 32-bit IEEE float (format code 3), mono or
/// interleaved multichannel. The header carries a `fact` chunk with the frame
/// count, and both size fields are patched in close().
class WavFileWriter {
public:
    /// RIFF + fmt + fact + data headers written before any sample.
    static constexpr long kHeaderBytes = 56;
    /// Offset of the patched `fact` frame count.
    static constexpr long kFactFramesOffset = 44;
    /// Offset of the patched data-chunk byte count.
    static constexpr long kDataBytesOffset = 52;

    WavFileWriter() = default;
    ~WavFileWriter();
    WavFileWriter(const WavFileWriter&) = delete;
    WavFileWriter& operator=(const WavFileWriter&) = delete;

    bool open(const std::string& path, int channels, uint32_t sampleRate);
    /// Writes `frames` frames of float samples in `channels`-interleaved order.
    bool writeFrames(const float* interleaved, int frames);
    /// Patches the size fields and closes; false on any I/O failure.
    bool close();

    int channels() const { return mChannels; }
    uint64_t frames() const { return mFrames; }

private:
    std::FILE* mFile = nullptr;
    uint64_t mFrames = 0;
    int mChannels = 0;
};

/// Multitrack take recorder. writeBlock() only copies into a bounded queue under
/// a mutex, so it is safe to call from the audio thread; a background thread
/// drains the queue into one mono WAV per armed input plus a stereo master WAV.
/// A full queue drops the block and bumps the drop counter.
class Recorder {
public:
    /// Blocks buffered between the audio thread and the writer thread.
    static constexpr int kQueueBlocks = 128;

    struct Take {
        /// Take directory, `take-<utc>/` under the requested base directory.
        std::string path;
        /// WAV files that closed cleanly: armed inputs plus master.wav when
        /// master recording was armed.
        int files = 0;
        uint64_t frames = 0;
        uint64_t drops = 0;
    };

    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    /// Creates `dir/take-<utc>/` (`-N` suffix on a same-second collision) and
    /// opens `chNN.wav` for every bit set in `armMask`, plus `master.wav` when
    /// `masterArm` is true (the default). With `masterArm == false` the master
    /// bus is not recorded and the take reports only the channel files.
    /// Returns false when a take is already running, `armMask` is empty, or the
    /// directory/files cannot be created.
    bool startTake(const std::string& dir, uint32_t armMask, uint32_t sampleRate = 48000,
                   bool masterArm = true);
    /// Queues one block (`n` <= kBlockSize) of channel taps and the master bus.
    /// Never blocks on disk and never allocates.
    void writeBlock(const float* const* taps, const float* masterL, const float* masterR, int n);
    /// Stops the writer, drains the queue, closes the WAVs and reports the take.
    /// Returns an empty Take when no take is running.
    Take stopTake();

    uint64_t drops() const { return mDrops.load(std::memory_order_relaxed); }
    bool recording() const { return mRunning.load(std::memory_order_acquire); }

private:
    struct Block {
        int n = 0;
        float master[2][kBlockSize] = {};
        float ch[kMaxInputs][kBlockSize] = {};
    };

    void writerLoop();
    void writeBlockToDisk(const Block& block);

    std::vector<Block> mQueue;
    /// Writer-thread-only staging block, so draining never holds the mutex.
    Block mScratch;
    std::mutex mMutex;
    std::condition_variable mCv;
    std::thread mThread;
    int mHead = 0;
    int mTail = 0;
    int mCount = 0;
    bool mStop = false;
    uint32_t mArmMask = 0;
    bool mMasterArm = true;
    std::string mTakeDir;
    std::atomic<bool> mRunning{false};
    std::atomic<uint64_t> mDrops{0};
    std::atomic<uint64_t> mFrames{0};
    WavFileWriter mChannelFile[kMaxInputs];
    WavFileWriter mMasterFile;
};

}  // namespace mnky::audio
