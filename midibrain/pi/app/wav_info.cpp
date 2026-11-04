#include "wav_info.hpp"

#include <fstream>
#include <vector>

namespace mnky::app {
namespace {
constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;

std::uint32_t readU32(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readU16(const unsigned char* p) {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                    (static_cast<std::uint16_t>(p[1]) << 8));
}
}  // namespace

WavInfo readWavInfo(const std::filesystem::path& path) {
  WavInfo info;
  std::ifstream file(path, std::ios::binary);
  if (!file) return info;

  file.seekg(0, std::ios::end);
  const auto fileSize = static_cast<std::uint64_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  if (fileSize < 12) return info;

  unsigned char header[12] = {};
  file.read(reinterpret_cast<char*>(header), sizeof header);
  if (file.gcount() != static_cast<std::streamsize>(sizeof header)) return info;
  if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F' ||
      header[8] != 'W' || header[9] != 'A' || header[10] != 'V' || header[11] != 'E') {
    return info;
  }

  std::uint32_t dataBytes = 0;
  bool haveFormat = false;
  bool haveData = false;
  while (file && !(haveFormat && haveData)) {
    unsigned char chunk[8] = {};
    file.read(reinterpret_cast<char*>(chunk), sizeof chunk);
    if (file.gcount() != static_cast<std::streamsize>(sizeof chunk)) break;
    const std::uint32_t size = readU32(chunk + 4);
    const std::uint64_t remaining =
        fileSize >= static_cast<std::uint64_t>(file.tellg())
            ? fileSize - static_cast<std::uint64_t>(file.tellg())
            : 0;
    const bool isFmt = chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't' &&
                       chunk[3] == ' ';
    const bool isData = chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' &&
                        chunk[3] == 'a';
    if (isFmt) {
      if (size < 16 || size > remaining) return info;
      std::vector<unsigned char> fmt(size);
      file.read(reinterpret_cast<char*>(fmt.data()), static_cast<std::streamsize>(size));
      if (file.gcount() != static_cast<std::streamsize>(size)) return info;
      info.format = readU16(fmt.data());
      info.channels = readU16(fmt.data() + 2);
      info.sampleRate = readU32(fmt.data() + 4);
      info.bitsPerSample = readU16(fmt.data() + 14);
      haveFormat = true;
    } else if (isData) {
      if (size > remaining) return info;
      dataBytes = size;
      haveData = true;
      file.seekg(static_cast<std::streamoff>(size), std::ios::cur);
    } else {
      if (size > remaining) return info;
      file.seekg(static_cast<std::streamoff>(size), std::ios::cur);
    }
    // RIFF chunks are word-aligned.
    if ((size & 1u) != 0u) file.seekg(1, std::ios::cur);
  }

  if (!haveFormat || !haveData || info.channels == 0 || info.sampleRate == 0) return info;
  if (info.format != kFormatPcm && info.format != kFormatFloat) return info;
  if (info.bitsPerSample == 0) return info;
  const std::uint64_t frameBytes =
      static_cast<std::uint64_t>(info.channels) * (info.bitsPerSample / 8u);
  if (frameBytes == 0) return info;
  info.frames = dataBytes / frameBytes;
  info.ok = true;
  return info;
}

double wavDurationSeconds(const std::filesystem::path& path) {
  const auto info = readWavInfo(path);
  if (!info.ok || info.sampleRate == 0) return 0.0;
  return static_cast<double>(info.frames) / static_cast<double>(info.sampleRate);
}

}  // namespace mnky::app
