// Assert-style acceptance tests for the background peak scanner and its
// `.peaks` sidecar (no framework).
// Build: clang++ -std=c++20 -Wall -Wextra -Werror -I. test_peaks.cpp peaks.cpp
//        recorder.cpp -o /tmp/t_peaks -pthread
//
// Covers 256-frame min/max buckets, the stereo downmix, the coarse fold,
// partial last buckets, the sidecar round-trip, size and mtime invalidation,
// worker-thread progress, the completion callback and the failure paths that
// leave the UI with a placeholder.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "peaks.hpp"
#include "recorder.hpp"

namespace {

using namespace mnky::audio;

int gFailures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++gFailures;                                                     \
        }                                                                    \
    } while (0)

void checkNear(const char* what, double actual, double expected,
               double eps = 1e-6) {
    if (std::fabs(actual - expected) <= eps) return;
    std::printf("FAIL %s: actual %.9g expected %.9g\n", what, actual, expected);
    ++gFailures;
}

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           ("mnky_peaks_" + std::to_string(static_cast<long>(::getpid())));
}

bool writeWav(const std::filesystem::path& path, int channels,
              const std::vector<float>& interleaved) {
    WavFileWriter writer;
    if (!writer.open(path.string(), channels, 48000)) return false;
    const int frames = static_cast<int>(interleaved.size() /
                                        static_cast<std::size_t>(channels));
    if (!writer.writeFrames(interleaved.data(), frames)) {
        writer.close();
        return false;
    }
    return writer.close();
}

std::vector<float> constant(int frames, float value) {
    return std::vector<float>(static_cast<std::size_t>(frames), value);
}

constexpr std::uint64_t kBuckets = 4;
const int kBucketFrames = static_cast<int>(kPeakBucketFrames);

// -------------------------------------------------------- buckets and coarse

void testBucketsAndCoarse() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    // Bucket i alternates between +0.1*(i+1) and -0.05*(i+1).
    std::vector<float> samples;
    for (std::uint64_t bucket = 0; bucket < kBuckets; ++bucket) {
        for (int frame = 0; frame < kBucketFrames; ++frame) {
            const float scale = static_cast<float>(bucket + 1);
            samples.push_back(frame % 2 == 0 ? 0.1f * scale : -0.05f * scale);
        }
    }
    const std::string take = (root / "buckets.wav").string();
    CHECK(writeWav(take, 1, samples));

    PeakScanner scanner;
    CHECK(scanner.summary() == nullptr);  // no scan yet: the UI placeholder
    CHECK(!scanner.busy());
    std::atomic<bool> done{false};
    std::atomic<bool> doneOk{false};
    scanner.start(take, {},
                  [&](bool ok, const std::string&) {
                      doneOk.store(ok);
                      done.store(true);
                  });
    scanner.wait();
    CHECK(done.load());
    CHECK(doneOk.load());
    CHECK(!scanner.busy());
    CHECK(scanner.lastError().empty());
    checkNear("completed scan reports full progress", scanner.progress(), 1.0);

    const PeakSummary* summary = scanner.summary();
    CHECK(summary != nullptr);
    if (summary == nullptr) return;
    CHECK(summary->source == take);
    CHECK(summary->sourceFrames == kBuckets * kBucketFrames);
    CHECK(summary->bucketFrames == kPeakBucketFrames);
    CHECK(summary->coarseFactor == kPeakCoarseFactor);
    CHECK(summary->buckets.size() == kBuckets);
    CHECK(summary->coarse.size() == 1);
    CHECK(!summary->fromSidecar);
    for (std::uint64_t bucket = 0;
         bucket < kBuckets && bucket < summary->buckets.size(); ++bucket) {
        const float scale = static_cast<float>(bucket + 1);
        checkNear("bucket max", summary->buckets[bucket].max, 0.1 * scale);
        checkNear("bucket min", summary->buckets[bucket].min, -0.05 * scale);
    }
    if (!summary->coarse.empty()) {
        checkNear("coarse max folds the loudest bucket",
                  summary->coarse[0].max, 0.4);
        checkNear("coarse min folds the quietest bucket",
                  summary->coarse[0].min, -0.2);
    }
    CHECK(std::filesystem::exists(peaksPath(take)));

    std::filesystem::remove_all(root, ec);
    std::printf("PASS peaks: 256-frame buckets and coarse fold\n");
}

// --------------------------------------------------- partial bucket / stereo

void testPartialBucketAndStereo() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    // 70 full buckets plus 100 frames: 71 buckets, the last one partial.
    std::vector<float> samples;
    for (int bucket = 0; bucket < 70; ++bucket) {
        const float value = static_cast<float>(bucket) / 1000.0f;
        for (int frame = 0; frame < kBucketFrames; ++frame) {
            samples.push_back(value);
        }
    }
    for (int frame = 0; frame < 100; ++frame) {
        samples.push_back(70.0f / 1000.0f);
    }
    const std::string take = (root / "partial.wav").string();
    CHECK(writeWav(take, 1, samples));

    PeakScanner scanner;
    scanner.start(take, {}, {});
    scanner.wait();
    const PeakSummary* summary = scanner.summary();
    CHECK(summary != nullptr);
    if (summary != nullptr) {
        CHECK(summary->buckets.size() == 71);
        checkNear("last bucket keeps the partial frames", summary->buckets[70].max,
                  0.07);
        // ceil(71 / 64) = 2 coarse points; the second folds buckets 64..70.
        CHECK(summary->coarse.size() == 2);
        if (summary->coarse.size() == 2) {
            checkNear("coarse folds the first 64 buckets", summary->coarse[0].max,
                      0.063);
            checkNear("coarse folds the trailing buckets",
                      summary->coarse[1].min, 0.064);
            checkNear("coarse folds the trailing buckets (max)",
                      summary->coarse[1].max, 0.07);
        }
    }

    // Stereo is the take voice's mono downmix, not the raw channels.
    std::vector<float> interleaved;
    for (int frame = 0; frame < kBucketFrames; ++frame) {
        interleaved.push_back(0.8f);
        interleaved.push_back(0.2f);
    }
    const std::string stereo = (root / "stereo.wav").string();
    CHECK(writeWav(stereo, 2, interleaved));
    PeakScanner downmix;
    downmix.start(stereo, {}, {});
    downmix.wait();
    const PeakSummary* mono = downmix.summary();
    CHECK(mono != nullptr);
    if (mono != nullptr && !mono->buckets.empty()) {
        checkNear("stereo buckets use 0.5 * (L + R)", mono->buckets[0].max, 0.5);
        checkNear("stereo buckets use 0.5 * (L + R) (min)",
                  mono->buckets[0].min, 0.5);
    }

    std::filesystem::remove_all(root, ec);
    std::printf("PASS peaks: partial buckets and stereo downmix\n");
}

// ---------------------------------------------- sidecar round-trip / invalidate

void testSidecarRoundTripAndInvalidation() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "take.wav").string();
    CHECK(writeWav(take, 1, constant(2 * kBucketFrames, 0.5f)));

    // Nothing cached yet: readPeaks() must say so instead of inventing data.
    PeakSummary missing;
    std::string reason;
    CHECK(!readPeaks(take, missing, &reason));
    CHECK(!reason.empty());

    PeakScanner scanner;
    scanner.start(take, {}, {});
    scanner.wait();
    const PeakSummary* scanned = scanner.summary();
    CHECK(scanned != nullptr);
    if (scanned == nullptr) return;
    const std::vector<PeakRange> buckets = scanned->buckets;
    const std::vector<PeakRange> coarse = scanned->coarse;
    CHECK(std::filesystem::exists(peaksPath(take)));

    // The written sidecar reads back bit-exactly and marks its provenance.
    PeakSummary loaded;
    CHECK(readPeaks(take, loaded, &reason));
    CHECK(loaded.fromSidecar);
    CHECK(loaded.sourceFrames == scanned->sourceFrames);
    CHECK(loaded.buckets == buckets);
    CHECK(loaded.coarse == coarse);

    // A second scanner reuses the sidecar instead of decoding the file.
    PeakScanner cached;
    cached.start(take, {}, {});
    cached.wait();
    CHECK(cached.summary() != nullptr);
    if (cached.summary() != nullptr) {
        CHECK(cached.summary()->fromSidecar);
        CHECK(cached.summary()->buckets == buckets);
    }

    // Size change: the cache is stale and must be rescanned, not trusted.
    CHECK(writeWav(take, 1, constant(3 * kBucketFrames, 0.5f)));
    CHECK(!readPeaks(take, loaded, &reason));
    CHECK(reason.find("stale") != std::string::npos);
    PeakScanner rescan;
    rescan.start(take, {}, {});
    rescan.wait();
    CHECK(rescan.summary() != nullptr);
    if (rescan.summary() != nullptr) {
        CHECK(!rescan.summary()->fromSidecar);
        CHECK(rescan.summary()->buckets.size() == 3);
    }

    // Timestamp-only change at the same size is a stale cache too.
    std::error_code stampError;
    const auto original = std::filesystem::last_write_time(take, stampError);
    CHECK(!stampError);
    std::filesystem::last_write_time(take, original + std::chrono::seconds(5),
                                    stampError);
    CHECK(!stampError);
    CHECK(!readPeaks(take, loaded, &reason));
    CHECK(reason.find("stale") != std::string::npos);

    // A corrupt or truncated sidecar is rejected rather than reinterpreted.
    {
        std::FILE* file = std::fopen(peaksPath(take).c_str(), "wb");
        CHECK(file != nullptr);
        if (file != nullptr) {
            const char junk[8] = {'n', 'o', 't', 'a', 'p', 'k', 0, 0};
            CHECK(std::fwrite(junk, 1, sizeof junk, file) == sizeof junk);
            std::fclose(file);
        }
    }
    CHECK(!readPeaks(take, loaded, &reason));
    CHECK(reason.find("too short") != std::string::npos ||
          reason.find("not a peak sidecar") != std::string::npos);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS peaks: sidecar round-trip and invalidation\n");
}

// ------------------------------------------------------- progress / threading

void testProgressAndWorkerThread() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const std::string take = (root / "long.wav").string();
    CHECK(writeWav(take, 1, constant(96000, 0.25f)));

    const std::thread::id caller = std::this_thread::get_id();
    std::mutex mutex;
    std::vector<float> fractions;
    std::vector<std::thread::id> threads;
    std::atomic<bool> done{false};

    PeakScanner scanner;
    scanner.start(
        take,
        [&](float fraction) {
            std::lock_guard<std::mutex> lock(mutex);
            fractions.push_back(fraction);
            threads.push_back(std::this_thread::get_id());
        },
        [&](bool, const std::string&) { done.store(true); });
    scanner.wait();
    CHECK(done.load());

    std::lock_guard<std::mutex> lock(mutex);
    CHECK(fractions.size() >= 2);
    if (!fractions.empty()) {
        checkNear("progress starts at zero", fractions.front(), 0.0);
        checkNear("progress ends at one", fractions.back(), 1.0);
        bool monotonic = true;
        bool offThread = true;
        for (std::size_t i = 0; i < fractions.size(); ++i) {
            if (i > 0 && fractions[i] < fractions[i - 1]) monotonic = false;
            if (threads[i] == caller) offThread = false;
        }
        CHECK(monotonic);
        CHECK(offThread);  // the scan never runs on the calling thread
    }
    checkNear("progress() reports the finished fraction", scanner.progress(),
              1.0);

    // cancel() with an idle scanner is a no-op; the published summary stays.
    const PeakSummary* before = scanner.summary();
    scanner.cancel();
    CHECK(!scanner.busy());
    CHECK(scanner.summary() == before);

    std::filesystem::remove_all(root, ec);
    std::printf("PASS peaks: worker-thread progress and completion\n");
}

// ------------------------------------------------------------- failure paths

void testFailurePaths() {
    const std::filesystem::path root = testRoot();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    PeakScanner scanner;
    CHECK(scanner.summary() == nullptr);  // UI placeholder before any scan

    // A missing file completes with a reason and publishes nothing.
    std::atomic<bool> done{false};
    std::atomic<bool> doneOk{true};
    scanner.start((root / "absent.wav").string(), {},
                  [&](bool ok, const std::string&) {
                      doneOk.store(ok);
                      done.store(true);
                  });
    scanner.wait();
    CHECK(done.load());
    CHECK(!doneOk.load());
    CHECK(scanner.summary() == nullptr);
    CHECK(!scanner.lastError().empty());
    CHECK(!std::filesystem::exists(peaksPath((root / "absent.wav").string())));

    // A non-48 kHz take is refused for the same reason the take voice refuses it.
    const std::string bad = (root / "44k.wav").string();
    {
        WavFileWriter writer;
        CHECK(writer.open(bad, 1, 44100));
        const std::vector<float> samples = constant(kBucketFrames, 0.5f);
        CHECK(writer.writeFrames(samples.data(), kBucketFrames));
        CHECK(writer.close());
    }
    PeakScanner refused;
    refused.start(bad, {}, {});
    refused.wait();
    CHECK(refused.summary() == nullptr);
    CHECK(refused.lastError().find("48000") != std::string::npos);
    CHECK(!std::filesystem::exists(peaksPath(bad)));

    std::filesystem::remove_all(root, ec);
    std::printf("PASS peaks: failure paths leave the UI placeholder\n");
}

}  // namespace

int main() {
    testBucketsAndCoarse();
    testPartialBucketAndStereo();
    testSidecarRoundTripAndInvalidation();
    testProgressAndWorkerThread();
    testFailurePaths();
    if (gFailures != 0) {
        std::printf("%d peak check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("all peak checks passed\n");
    return 0;
}
