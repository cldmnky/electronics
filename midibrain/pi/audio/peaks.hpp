#pragma once
// Background peak scanner and `.peaks` sidecar for the waveform editor
// (JUCE-free, portable C++20).
//
// The overview is display data only: take playback and region edits never read
// it, so a missing, stale or unreadable sidecar costs the UI its waveform and
// nothing else. Scans run on their own thread and publish an immutable summary
// with one atomic pointer store, so neither the audio thread (which never reads
// peaks) nor a UI paint pass ever waits for a scan.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mnky::audio {

/// Frames per overview bucket (5.3 ms at 48 kHz).
inline constexpr std::uint32_t kPeakBucketFrames = 256;
/// Buckets folded into one coarse overview point, i.e. a zoomed-out level with
/// no per-bucket work in the paint pass (~341 ms at 48 kHz).
inline constexpr std::uint32_t kPeakCoarseFactor = 64;
/// `.peaks` layout version; a mismatch rescans instead of reinterpreting bytes.
inline constexpr std::uint32_t kPeakVersion = 1;

/// Extreme mono samples of one window. The mono value is the take voice's own
/// signal (stereo is downmixed 0.5 * (L + R)), so the drawing matches playback.
struct PeakRange {
    float min = 0.0f;
    float max = 0.0f;
    friend bool operator==(const PeakRange&, const PeakRange&) = default;
};

/// Immutable overview of one take file. `buckets[i]` covers source frames
/// [i * bucketFrames, (i + 1) * bucketFrames); `coarse[i]` folds
/// `coarseFactor` buckets so a zoomed-out view needs no reduction.
struct PeakSummary {
    std::string source;               // WAV the overview was scanned from
    std::uint64_t sourceFrames = 0;
    std::uint64_t sourceBytes = 0;    // freshness key: file size
    std::uint64_t sourceMtime = 0;    // freshness key: last_write_time ticks
    std::uint32_t bucketFrames = kPeakBucketFrames;
    std::uint32_t coarseFactor = kPeakCoarseFactor;
    std::vector<PeakRange> buckets;
    std::vector<PeakRange> coarse;
    /// True when the summary was loaded from a fresh sidecar, not scanned.
    bool fromSidecar = false;

    bool empty() const { return buckets.empty(); }
};

/// Sidecar path for a take: the WAV path plus `.peaks`, next to the file.
std::string peaksPath(const std::string& wavPath);

/// Reads `wavPath`'s sidecar and accepts it only when its magic, version, bucket
/// configuration and recorded source size/mtime all match the current WAV. That
/// is exactly the "reuse it" case, so any failure (missing, truncated, stale,
/// different config) means "rescan" and writes the reason to `error`. Never
/// throws and never logs.
bool readPeaks(const std::string& wavPath, PeakSummary& summary,
               std::string* error = nullptr);

/// Writes the sidecar next to `wavPath` through a temporary file and a rename,
/// so an interrupted write can never leave a sidecar that passes readPeaks().
/// Returns false with a reason on I/O failure; that only costs the cache.
bool writePeaks(const std::string& wavPath, const PeakSummary& summary,
                std::string* error = nullptr);

/// Scans one take file on a worker thread.
///
/// `start()` first tries the sidecar and publishes it without touching the
/// audio data when it is fresh; otherwise it decodes the file in chunks of
/// kPeakBucketFrames, folds `coarseFactor` buckets into the coarse level, writes
/// the sidecar and publishes the summary. Progress is an atomic fraction and the
/// finished summary is handed over with one release store of an immutable
/// pointer; the scanner keeps every published generation alive, so a pointer
/// taken by a paint pass stays valid. Both callbacks run on the worker thread;
/// `progress` runs about a hundred times per scan plus once at 0 and once at 1.
///
/// start(), cancel() and wait() are control-thread calls and are not safe to run
/// concurrently with each other; summary(), progress() and busy() may be read
/// from any thread. cancel() drops the scan without a done callback.
class PeakScanner {
public:
    using ProgressCallback = std::function<void(float fraction)>;
    using DoneCallback = std::function<void(bool ok, const std::string& error)>;

    PeakScanner() = default;
    ~PeakScanner();
    PeakScanner(const PeakScanner&) = delete;
    PeakScanner& operator=(const PeakScanner&) = delete;

    /// Starts (or restarts) a scan of `wavPath`. Any scan still running is
    /// cancelled first, so `progress()` and `summary()` always describe the
    /// newest request.
    void start(const std::string& wavPath, ProgressCallback progress = {},
               DoneCallback done = {});
    /// Stops the worker without publishing and without a done callback.
    void cancel();
    /// Joins the worker (no-op when idle), so a control-thread test or shutdown
    /// can wait for completion without polling.
    void wait();

    bool busy() const { return mBusy.load(std::memory_order_acquire); }
    /// Fraction of the current (or last) scan, 0..1.
    float progress() const { return mProgress.load(std::memory_order_relaxed); }
    /// Latest published overview, or nullptr before the first scan finishes -
    /// the UI draws its placeholder then. Never blocks.
    const PeakSummary* summary() const {
        return mCurrent.load(std::memory_order_acquire);
    }
    /// Reason the last scan failed; empty when it succeeded.
    std::string lastError() const;

private:
    void worker(std::string wavPath);
    bool scanFile(const std::string& wavPath, PeakSummary& summary,
                  std::string& error);
    void publish(PeakSummary summary);
    void report(float fraction);
    void stopWorker();

    std::thread mThread;
    std::atomic<bool> mBusy{false};
    std::atomic<bool> mStop{false};
    std::atomic<float> mProgress{0.0f};
    std::atomic<const PeakSummary*> mCurrent{nullptr};
    // Published summaries stay owned by the scanner: a reader's pointer never
    // dangles, at the cost of keeping the (small) overviews it has produced.
    std::vector<std::unique_ptr<PeakSummary>> mPublished;
    ProgressCallback mOnProgress;
    DoneCallback mOnDone;
    mutable std::mutex mErrorMutex;
    std::string mError;
};

}  // namespace mnky::audio
