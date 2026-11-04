#include "peaks.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

#include "wav.hpp"

namespace mnky::audio {
namespace {

constexpr std::size_t kHeaderBytes = 56;
constexpr char kMagic[4] = {'M', 'N', 'P', 'K'};
// Frames decoded per chunk inside the scanner (16 buckets).
constexpr int kScanChunkFrames = 4096;
// Progress callbacks are throttled to about a hundred per scan.
constexpr float kProgressStep = 0.01f;

void putU32(unsigned char* out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value & 0xffu);
    out[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    out[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    out[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

void putU64(unsigned char* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xffu);
    }
}

std::uint32_t getU32(const unsigned char* in) {
    return static_cast<std::uint32_t>(in[0]) |
           (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) |
           (static_cast<std::uint32_t>(in[3]) << 24);
}

std::uint64_t getU64(const unsigned char* in) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | static_cast<std::uint64_t>(in[i]);
    }
    return value;
}

void putFloat(unsigned char* out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    putU32(out, bits);
}

float getFloat(const unsigned char* in) {
    const std::uint32_t bits = getU32(in);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

/// Size and last-write tick of a file. Returns false when the file is missing;
/// the tick is the platform's file_time_type count, which is only ever compared
/// with itself (a sidecar is a same-machine cache, not a portable record).
bool sourceStats(const std::string& path, std::uint64_t& bytes,
                 std::uint64_t& mtime, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "cannot measure " + wav_detail::quoted(path) + " (" + ec.message() + ")";
        return false;
    }
    const std::filesystem::file_time_type stamp =
        std::filesystem::last_write_time(path, ec);
    if (ec) {
        error = "cannot read the timestamp of " + wav_detail::quoted(path) + " (" +
                ec.message() + ")";
        return false;
    }
    bytes = static_cast<std::uint64_t>(size);
    mtime = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(stamp.time_since_epoch().count()));
    return true;
}

}  // namespace

std::string peaksPath(const std::string& wavPath) { return wavPath + ".peaks"; }

bool readPeaks(const std::string& wavPath, PeakSummary& summary,
               std::string* error) {
    const auto fail = [error](const std::string& reason) {
        if (error != nullptr) *error = reason;
        return false;
    };
    std::uint64_t sourceBytes = 0;
    std::uint64_t sourceMtime = 0;
    std::string reason;
    if (!sourceStats(wavPath, sourceBytes, sourceMtime, reason)) {
        return fail(reason);
    }
    const std::string path = peaksPath(wavPath);
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return fail("no peak sidecar next to " + wav_detail::quoted(wavPath));
    }
    unsigned char header[kHeaderBytes] = {};
    if (std::fread(header, 1, sizeof header, file) != sizeof header) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is too short to be a peak sidecar");
    }
    if (std::memcmp(header, kMagic, sizeof kMagic) != 0) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is not a peak sidecar");
    }
    const std::uint32_t version = getU32(header + 4);
    const std::uint32_t bucketFrames = getU32(header + 8);
    const std::uint32_t coarseFactor = getU32(header + 12);
    const std::uint64_t sourceFrames = getU64(header + 16);
    const std::uint64_t recordedBytes = getU64(header + 24);
    const std::uint64_t recordedMtime = getU64(header + 32);
    const std::uint64_t bucketCount = getU64(header + 40);
    const std::uint64_t coarseCount = getU64(header + 48);

    if (version != kPeakVersion || bucketFrames != kPeakBucketFrames ||
        coarseFactor != kPeakCoarseFactor) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) +
                    " uses another peak layout or bucket size; rescanning");
    }
    if (recordedBytes != sourceBytes || recordedMtime != sourceMtime) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is stale for " +
                    wav_detail::quoted(wavPath) + "; rescanning");
    }
    // The declared counts must fit the sidecar itself, so a corrupt header can
    // never ask for a huge allocation.
    std::error_code sizeError;
    const std::uintmax_t sidecarBytes =
        std::filesystem::file_size(path, sizeError);
    if (sizeError || sidecarBytes < kHeaderBytes) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is truncated");
    }
    const std::uint64_t entries =
        (static_cast<std::uint64_t>(sidecarBytes) - kHeaderBytes) / 8;
    if (bucketCount > entries || coarseCount > entries - bucketCount) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is truncated");
    }
    std::vector<unsigned char> bucketBytes;
    std::vector<unsigned char> coarseBytes;
    try {
        bucketBytes.resize(static_cast<std::size_t>(bucketCount * 8));
        coarseBytes.resize(static_cast<std::size_t>(coarseCount * 8));
    } catch (...) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is too large to load");
    }
    if (std::fread(bucketBytes.data(), 1, bucketBytes.size(), file) !=
            bucketBytes.size() ||
        std::fread(coarseBytes.data(), 1, coarseBytes.size(), file) !=
            coarseBytes.size()) {
        std::fclose(file);
        return fail(wav_detail::quoted(path) + " is truncated");
    }
    std::fclose(file);

    PeakSummary found;
    found.source = wavPath;
    found.sourceFrames = sourceFrames;
    found.sourceBytes = recordedBytes;
    found.sourceMtime = recordedMtime;
    found.bucketFrames = bucketFrames;
    found.coarseFactor = coarseFactor;
    found.fromSidecar = true;
    try {
        found.buckets.resize(static_cast<std::size_t>(bucketCount));
        found.coarse.resize(static_cast<std::size_t>(coarseCount));
    } catch (...) {
        return fail(wav_detail::quoted(path) + " is too large to load");
    }
    for (std::size_t i = 0; i < found.buckets.size(); ++i) {
        const unsigned char* entry = bucketBytes.data() + i * 8;
        found.buckets[i].min = getFloat(entry);
        found.buckets[i].max = getFloat(entry + 4);
    }
    for (std::size_t i = 0; i < found.coarse.size(); ++i) {
        const unsigned char* entry = coarseBytes.data() + i * 8;
        found.coarse[i].min = getFloat(entry);
        found.coarse[i].max = getFloat(entry + 4);
    }
    summary = std::move(found);
    return true;
}

bool writePeaks(const std::string& wavPath, const PeakSummary& summary,
                std::string* error) {
    const auto fail = [error](const std::string& reason) {
        if (error != nullptr) *error = reason;
        return false;
    };
    const std::string path = peaksPath(wavPath);
    const std::string temp = path + ".tmp";
    std::FILE* file = std::fopen(temp.c_str(), "wb");
    if (file == nullptr) return fail("cannot write " + wav_detail::quoted(temp));

    unsigned char header[kHeaderBytes] = {};
    std::memcpy(header, kMagic, sizeof kMagic);
    putU32(header + 4, kPeakVersion);
    putU32(header + 8, summary.bucketFrames);
    putU32(header + 12, summary.coarseFactor);
    putU64(header + 16, summary.sourceFrames);
    putU64(header + 24, summary.sourceBytes);
    putU64(header + 32, summary.sourceMtime);
    putU64(header + 40, summary.buckets.size());
    putU64(header + 48, summary.coarse.size());
    bool ok = std::fwrite(header, 1, sizeof header, file) == sizeof header;
    unsigned char entry[8] = {};
    for (std::size_t i = 0; ok && i < summary.buckets.size(); ++i) {
        putFloat(entry, summary.buckets[i].min);
        putFloat(entry + 4, summary.buckets[i].max);
        ok = std::fwrite(entry, 1, sizeof entry, file) == sizeof entry;
    }
    for (std::size_t i = 0; ok && i < summary.coarse.size(); ++i) {
        putFloat(entry, summary.coarse[i].min);
        putFloat(entry + 4, summary.coarse[i].max);
        ok = std::fwrite(entry, 1, sizeof entry, file) == sizeof entry;
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        std::remove(temp.c_str());
        return fail("cannot write " + wav_detail::quoted(temp));
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return fail("cannot replace " + wav_detail::quoted(path));
    }
    return true;
}

PeakScanner::~PeakScanner() { stopWorker(); }

void PeakScanner::start(const std::string& wavPath, ProgressCallback progress,
                        DoneCallback done) {
    stopWorker();
    {
        std::lock_guard<std::mutex> lock(mErrorMutex);
        mError.clear();
    }
    mOnProgress = std::move(progress);
    mOnDone = std::move(done);
    mProgress.store(0.0f, std::memory_order_relaxed);
    mBusy.store(true, std::memory_order_release);
    mThread = std::thread(&PeakScanner::worker, this, wavPath);
}

void PeakScanner::cancel() { stopWorker(); }

void PeakScanner::wait() {
    if (mThread.joinable()) mThread.join();
    mBusy.store(false, std::memory_order_release);
}

std::string PeakScanner::lastError() const {
    std::lock_guard<std::mutex> lock(mErrorMutex);
    return mError;
}

void PeakScanner::stopWorker() {
    mStop.store(true, std::memory_order_release);
    if (mThread.joinable()) mThread.join();
    mStop.store(false, std::memory_order_release);
    mBusy.store(false, std::memory_order_release);
}

void PeakScanner::report(float fraction) {
    mProgress.store(fraction, std::memory_order_relaxed);
    if (mOnProgress) mOnProgress(fraction);
}

void PeakScanner::publish(PeakSummary summary) {
    std::unique_ptr<PeakSummary> owned =
        std::make_unique<PeakSummary>(std::move(summary));
    const PeakSummary* raw = owned.get();
    mPublished.push_back(std::move(owned));
    mCurrent.store(raw, std::memory_order_release);
}

bool PeakScanner::scanFile(const std::string& wavPath, PeakSummary& summary,
                           std::string& error) {
    WavInfo info;
    if (!inspectTakeWav(wavPath, info, error)) return false;
    std::uint64_t sourceBytes = 0;
    std::uint64_t sourceMtime = 0;
    if (!sourceStats(wavPath, sourceBytes, sourceMtime, error)) return false;
    std::FILE* file = std::fopen(wavPath.c_str(), "rb");
    if (file == nullptr) {
        error = "cannot open " + wav_detail::quoted(wavPath);
        return false;
    }
    if (info.dataOffset > static_cast<std::uint64_t>(LONG_MAX) ||
        std::fseek(file, static_cast<long>(info.dataOffset), SEEK_SET) != 0) {
        std::fclose(file);
        error = "cannot seek " + wav_detail::quoted(wavPath);
        return false;
    }

    const std::uint64_t frames = info.frames();
    PeakSummary scanned;
    scanned.source = wavPath;
    scanned.sourceFrames = frames;
    scanned.sourceBytes = sourceBytes;
    scanned.sourceMtime = sourceMtime;
    scanned.bucketFrames = kPeakBucketFrames;
    scanned.coarseFactor = kPeakCoarseFactor;
    scanned.fromSidecar = false;
    scanned.buckets.reserve(static_cast<std::size_t>(
        (frames + kPeakBucketFrames - 1) / kPeakBucketFrames));

    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    PeakRange bucket{kInfinity, -kInfinity};
    std::uint32_t filled = 0;
    std::uint64_t produced = 0;
    float lastReported = 0.0f;
    float chunk[kScanChunkFrames] = {};
    while (produced < frames) {
        if (mStop.load(std::memory_order_acquire)) {
            std::fclose(file);
            error = "scan cancelled";
            return false;
        }
        const int want = static_cast<int>(std::min<std::uint64_t>(
            frames - produced, static_cast<std::uint64_t>(kScanChunkFrames)));
        const int got = decodeTakeFrames(file, info.channels, info.bits / 8u,
                                         info.floatSamples(), chunk, want);
        if (got <= 0) break;  // the data chunk is shorter than the header says
        for (int i = 0; i < got; ++i) {
            bucket.min = std::min(bucket.min, chunk[i]);
            bucket.max = std::max(bucket.max, chunk[i]);
            if (++filled == kPeakBucketFrames) {
                scanned.buckets.push_back(bucket);
                bucket = PeakRange{kInfinity, -kInfinity};
                filled = 0;
            }
        }
        produced += static_cast<std::uint64_t>(got);
        const float fraction =
            static_cast<float>(static_cast<double>(produced) /
                               static_cast<double>(frames));
        if (fraction - lastReported >= kProgressStep) {
            lastReported = fraction;
            report(fraction);
        }
    }
    std::fclose(file);
    if (filled > 0) scanned.buckets.push_back(bucket);

    // A bucket whose samples were all NaN keeps its sentinels; store silence so
    // the drawing has a finite value instead of an infinity.
    const auto fold = [](PeakRange& range) {
        if (!(range.min <= range.max)) range = PeakRange{0.0f, 0.0f};
    };
    for (PeakRange& range : scanned.buckets) fold(range);
    for (std::size_t i = 0; i < scanned.buckets.size();
         i += kPeakCoarseFactor) {
        PeakRange folded = scanned.buckets[i];
        const std::size_t end =
            std::min(i + kPeakCoarseFactor, scanned.buckets.size());
        for (std::size_t j = i + 1; j < end; ++j) {
            folded.min = std::min(folded.min, scanned.buckets[j].min);
            folded.max = std::max(folded.max, scanned.buckets[j].max);
        }
        scanned.coarse.push_back(folded);
    }
    summary = std::move(scanned);
    return true;
}

void PeakScanner::worker(std::string wavPath) {
    report(0.0f);
    PeakSummary summary;
    std::string error;
    bool ok = false;
    // A fresh sidecar makes the scan a sidecar read; anything else rescans.
    if (readPeaks(wavPath, summary, &error)) {
        summary.fromSidecar = true;
        ok = true;
        // A repeated request for the same unchanged file keeps the published
        // generation instead of retaining another identical copy.
        const PeakSummary* current = mCurrent.load(std::memory_order_acquire);
        if (current != nullptr && current->source == summary.source &&
            current->sourceBytes == summary.sourceBytes &&
            current->sourceMtime == summary.sourceMtime &&
            current->sourceFrames == summary.sourceFrames &&
            current->buckets.size() == summary.buckets.size()) {
            if (mStop.load(std::memory_order_acquire)) {
                mBusy.store(false, std::memory_order_release);
                return;
            }
            report(1.0f);
            mBusy.store(false, std::memory_order_release);
            if (mOnDone) mOnDone(true, error);
            return;
        }
        error.clear();
    } else if (!mStop.load(std::memory_order_acquire)) {
        ok = scanFile(wavPath, summary, error);
        if (ok && !mStop.load(std::memory_order_acquire)) {
            std::string writeError;
            if (!writePeaks(wavPath, summary, &writeError)) {
                // A read-only take directory costs the cache, not the waveform.
                std::fprintf(stderr, "mnky audio: peaks: %s\n",
                             writeError.c_str());
            }
        }
    }
    if (mStop.load(std::memory_order_acquire)) {
        mBusy.store(false, std::memory_order_release);
        return;
    }
    if (ok) {
        publish(std::move(summary));
        report(1.0f);
        error.clear();
    } else {
        std::lock_guard<std::mutex> lock(mErrorMutex);
        mError = error;
    }
    mBusy.store(false, std::memory_order_release);
    if (mOnDone) mOnDone(ok, error);
}

}  // namespace mnky::audio
