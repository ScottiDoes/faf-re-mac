// Headless census of SFD movies through the engine's own decoder (package movies,
// milestone 3; notes: macos/porting/movies.md).
//
//   movie_census [--jobs N] [--png DIR] [--shot NAME@FRAME ...] [--tsv FILE] PATH...
//
// PATH is a .sfd file or a folder of them. Each movie is read the way sfd::Player::Open
// reads it: moho::sfd::ReadMetadata (SfdHeaderParser) for the header, the player's
// composition check, then moho::sfd::FfmpegDecoder (FfmpegSfdDecoder.cpp, the engine's
// TU unchanged) decoding every packet, video to BGRA and embedded audio to 48 kHz
// stereo S16, until both decoders and the resampler are drained. Checked per movie:
// decoded size equals the header's, BGRA is opaque, the decoded frame count against the
// header's, embedded audio present when declared, timestamps that go back (WARN: the
// player shows a late frame at once). Status REJECTED: special composition, which the
// player refuses at open; NOHEADER: a plain MPEG program stream without Sofdec's header
// (the player refuses it too; it is decoded anyway).
// --shot NAME@FRAME writes frame FRAME (0-based; "last" for the final one) of the movie
// whose file name is NAME to DIR/NAME_FRAME.png; --shot NAME@audio writes its decoded
// audio (48 kHz stereo S16, what the sink is given) to DIR/NAME_audio48k.wav. Exit status 1 if any movie the player
// would accept fails to decode.

#include "moho/movie/sfd/FfmpegSfdDecoder.h"
#include "moho/movie/sfd/SfdMetadata.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace moho::sfd;

namespace
{
  // --- PNG (stored deflate blocks; no zlib needed) -------------------------------------
  std::uint32_t Crc(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0)
  {
    static std::uint32_t table[256];
    static const bool ready = [] {
      for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k)
          c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
      }
      return true;
    }();
    (void)ready;
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
      crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
  }

  void Put32(std::vector<std::uint8_t>& out, std::uint32_t v)
  {
    for (int s = 24; s >= 0; s -= 8)
      out.push_back(static_cast<std::uint8_t>(v >> s));
  }

  void Chunk(std::ofstream& file, const char* type, const std::vector<std::uint8_t>& data)
  {
    std::vector<std::uint8_t> head;
    Put32(head, static_cast<std::uint32_t>(data.size()));
    file.write(reinterpret_cast<const char*>(head.data()), 4);
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    file.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    std::vector<std::uint8_t> crc;
    Put32(crc, Crc(body.data(), body.size()));
    file.write(reinterpret_cast<const char*>(crc.data()), 4);
  }

  void WritePng(const fs::path& path, const DecodedVideoFrame& frame)
  {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(frame.height) * (frame.width * 3 + 1));
    for (int y = 0; y < frame.height; ++y) {
      raw.push_back(0);
      const std::uint8_t* row = frame.bgra.data() + static_cast<std::size_t>(y) * frame.pitch;
      for (int x = 0; x < frame.width; ++x) {
        raw.push_back(row[x * 4 + 2]);
        raw.push_back(row[x * 4 + 1]);
        raw.push_back(row[x * 4 + 0]);
      }
    }
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
      a = (a + byte) % 65521;
      b = (b + a) % 65521;
    }
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
      const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
      const bool last = at + n == raw.size();
      z.push_back(last ? 1 : 0);
      z.push_back(static_cast<std::uint8_t>(n));
      z.push_back(static_cast<std::uint8_t>(n >> 8));
      z.push_back(static_cast<std::uint8_t>(~n));
      z.push_back(static_cast<std::uint8_t>(~n >> 8));
      z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
      at += n;
      if (last)
        break;
    }
    Put32(z, (b << 16) | a);
    std::ofstream file(path, std::ios::binary);
    static const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    file.write(reinterpret_cast<const char*>(signature), sizeof(signature));
    std::vector<std::uint8_t> ihdr;
    Put32(ihdr, static_cast<std::uint32_t>(frame.width));
    Put32(ihdr, static_cast<std::uint32_t>(frame.height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    Chunk(file, "IHDR", ihdr);
    Chunk(file, "IDAT", z);
    Chunk(file, "IEND", {});
    if (!file)
      throw std::runtime_error("cannot write " + path.string());
  }

  // --- One movie -------------------------------------------------------------------------
  struct Result
  {
    std::string path, name, status{"OK"}, reason;
    int width{}, height{}, fpsMilli{}, mode{}, effect{}, colour{}, picture{};
    unsigned headerFrames{}, audioStreams{};
    std::size_t frames{}, samples{};
    double videoEnd{}, audioEnd{}, seconds{};
    int audioRate{}, audioChannels{};
    bool playerRejects{};
    std::vector<std::string> shots;
  };

  Result Census(const fs::path& path, const std::map<std::string, std::vector<std::string>>& shots, const fs::path& pngDir)
  {
    Result r;
    r.path = path.string();
    r.name = path.filename().string();
    const auto start = std::chrono::steady_clock::now();
    try {
      Metadata metadata;
      std::string error;
      bool noHeader = false;
      if (!ReadMetadata(r.path.c_str(), metadata, error)) {
        // A plain MPEG program stream (pack start code 00 00 01 BA) without Sofdec's
        // header: decode it anyway to show what is in it, and report it apart.
        std::ifstream probe(path, std::ios::binary);
        unsigned char start[4]{};
        probe.read(reinterpret_cast<char*>(start), 4);
        if (!(start[0] == 0 && start[1] == 0 && start[2] == 1 && start[3] == 0xBA))
          throw std::runtime_error("header: " + error);
        noHeader = true;
      }
      const auto& h = metadata.header;
      r.width = h.width;
      r.height = h.height;
      r.fpsMilli = static_cast<int>(h.fpsMilli);
      r.headerFrames = h.frameCount;
      r.audioStreams = h.audioStreams;
      r.mode = h.compositionMode;
      r.effect = h.videos[0].effect;
      r.colour = h.videos[0].colourType;
      r.picture = h.videos[0].pictureType;
      // sfd::Player::Open refuses these before decoding (IndependentSfdBackend.cpp).
      r.playerRejects = noHeader || r.effect != 0 || r.colour != 0 || r.picture != 0;

      std::vector<std::string> wanted;
      if (const auto it = shots.find(r.name); it != shots.end())
        wanted = it->second;
      std::shared_ptr<DecodedVideoFrame> last;
      const bool dumpAudio = std::find(wanted.begin(), wanted.end(), "audio") != wanted.end();
      std::vector<std::int16_t> pcm;

      FfmpegDecoder decoder;
      decoder.Open(r.path.c_str(), true);
      DecodeBatch batch;
      double lastPts = -1e9, maxBack = 0;
      std::size_t backSteps = 0;
      while (decoder.Read(batch)) {
        for (const auto& f : batch.video) {
          if (noHeader && r.frames == 0) {
            r.width = f->width;
            r.height = f->height;
          }
          if (!noHeader && (f->width != h.width || f->height != h.height))
            throw std::runtime_error(
              "decoded " + std::to_string(f->width) + "x" + std::to_string(f->height) + " disagrees with header"
            );
          // sfd::Player shows frames by timestamp and does not require them to rise
          // (a late frame is shown at once); note the largest step back.
          if (f->pts < lastPts) {
            backSteps += 1;
            maxBack = std::max(maxBack, lastPts - f->pts);
          }
          lastPts = std::max(lastPts, f->pts);
          if (f->bgra.size() != static_cast<std::size_t>(f->pitch) * f->height)
            throw std::runtime_error("BGRA size mismatch");
          for (std::size_t i = 3; i < f->bgra.size(); i += 4)
            if (f->bgra[i] != 255)
              throw std::runtime_error("non-opaque BGRA");
          const std::string index = std::to_string(r.frames);
          if (std::find(wanted.begin(), wanted.end(), index) != wanted.end()) {
            const fs::path out = pngDir / (path.stem().string() + "_" + index + ".png");
            WritePng(out, *f);
            r.shots.push_back(out.string());
          }
          last = f;
          ++r.frames;
          r.videoEnd = f->pts + f->duration;
        }
        for (const auto& f : batch.audio) {
          if (dumpAudio)
            pcm.insert(pcm.end(), f.samples.begin(), f.samples.end());
          r.samples += f.samples.size() / 2;
          r.audioEnd = f.pts + static_cast<double>(f.samples.size()) / 96000.0;
        }
      }
      if (last && std::find(wanted.begin(), wanted.end(), "last") != wanted.end()) {
        const fs::path out = pngDir / (path.stem().string() + "_last.png");
        WritePng(out, *last);
        r.shots.push_back(out.string());
      }
      if (dumpAudio && !pcm.empty()) {
        // The decoder's output as the sink receives it: 48 kHz interleaved stereo S16.
        const fs::path out = pngDir / (path.stem().string() + "_audio48k.wav");
        std::ofstream wav(out, std::ios::binary);
        const auto le = [&wav](std::uint32_t v, int bytes) {
          for (int i = 0; i < bytes; ++i)
            wav.put(static_cast<char>((v >> (8 * i)) & 0xFF));
        };
        const std::uint32_t data = static_cast<std::uint32_t>(pcm.size() * 2);
        wav.write("RIFF", 4);
        le(36 + data, 4);
        wav.write("WAVEfmt ", 8);
        le(16, 4); le(1, 2); le(2, 2); le(48000, 4); le(192000, 4); le(4, 2); le(16, 2);
        wav.write("data", 4);
        le(data, 4);
        wav.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data));
        r.shots.push_back(out.string());
      }
      r.audioRate = decoder.AudioSampleRate();
      r.audioChannels = decoder.AudioChannels();
      if (r.frames == 0)
        throw std::runtime_error("no decoded frames");
      if (h.audioStreams && r.samples == 0)
        throw std::runtime_error("declared embedded audio, none decoded");
      if (r.headerFrames && r.frames != r.headerFrames) {
        r.status = "WARN";
        r.reason = "decoded " + std::to_string(r.frames) + " frames, header says " + std::to_string(r.headerFrames);
      }
      if (backSteps) {
        r.status = "WARN";
        r.reason += (r.reason.empty() ? "" : "; ") + std::to_string(backSteps) +
          " video timestamp(s) go back, at most " + std::to_string(maxBack) + " s";
      }
      if (noHeader) {
        r.status = "NOHEADER";
        r.reason = "plain MPEG program stream without the SofdecStream header (" + error +
          "): sfd::Player refuses it at open, as faf-re does on Windows; FFmpeg decodes it";
      } else if (r.playerRejects) {
        r.status = "REJECTED";
        r.reason = "special composition (mode " + std::to_string(r.mode) + ", effect " + std::to_string(r.effect) +
          "): sfd::Player refuses it at open, as on Windows" + (r.reason.empty() ? "" : "; " + r.reason);
      }
    } catch (const std::exception& e) {
      r.status = r.playerRejects ? "REJECTED" : "FAIL";
      r.reason = e.what();
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return r;
  }
} // namespace

int main(int argc, char** argv)
{
  unsigned jobs = std::max(1u, std::thread::hardware_concurrency() / 2);
  fs::path pngDir = ".";
  fs::path tsv;
  std::map<std::string, std::vector<std::string>> shots;
  std::vector<fs::path> files;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--jobs" && i + 1 < argc)
      jobs = static_cast<unsigned>(std::max(1, std::atoi(argv[++i])));
    else if (arg == "--png" && i + 1 < argc)
      pngDir = argv[++i];
    else if (arg == "--tsv" && i + 1 < argc)
      tsv = argv[++i];
    else if (arg == "--shot" && i + 1 < argc) {
      const std::string spec = argv[++i];
      const auto at = spec.rfind('@');
      if (at == std::string::npos) {
        std::cerr << "--shot NAME@FRAME\n";
        return 2;
      }
      shots[spec.substr(0, at)].push_back(spec.substr(at + 1));
    } else {
      const fs::path p = arg;
      if (fs::is_directory(p)) {
        for (const auto& e : fs::directory_iterator(p)) {
          auto ext = e.path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
          if (e.is_regular_file() && ext == ".sfd")
            files.push_back(e.path());
        }
      } else
        files.push_back(p);
    }
  }
  if (files.empty()) {
    std::cerr << "usage: movie_census [--jobs N] [--png DIR] [--shot NAME@FRAME ...] [--tsv FILE] PATH...\n";
    return 2;
  }
  // Largest first, so the long movies do not finish last on one thread.
  std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
    return fs::file_size(a) > fs::file_size(b);
  });
  fs::create_directories(pngDir);

  std::vector<Result> results(files.size());
  std::atomic<std::size_t> next{0};
  std::mutex print;
  std::vector<std::thread> workers;
  const auto start = std::chrono::steady_clock::now();
  for (unsigned j = 0; j < jobs; ++j)
    workers.emplace_back([&] {
      for (std::size_t i; (i = next++) < files.size();) {
        results[i] = Census(files[i], shots, pngDir);
        if (results[i].status != "OK") {
          std::lock_guard lock(print);
          std::cout << results[i].status << ' ' << results[i].path << ": " << results[i].reason << '\n';
        }
      }
    });
  for (auto& w : workers)
    w.join();
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) { return a.path < b.path; });
  std::map<std::string, std::size_t> byStatus;
  std::map<std::string, std::size_t> bySize;
  std::size_t frames = 0, withAudio = 0;
  double videoSeconds = 0;
  for (const auto& r : results) {
    ++byStatus[r.status];
    ++bySize[std::to_string(r.width) + "x" + std::to_string(r.height)];
    frames += r.frames;
    videoSeconds += r.videoEnd;
    withAudio += r.samples ? 1 : 0;
  }
  if (!tsv.empty()) {
    std::ofstream out(tsv);
    out << "file\tstatus\twidth\theight\tfps\theader_frames\tframes\tvideo_end_s\taudio_streams\taudio_src_hz\t"
           "audio_src_ch\tstereo48k_samples\taudio_end_s\tmode\teffect\tdecode_s\treason\n";
    for (const auto& r : results)
      out << r.path << '\t' << r.status << '\t' << r.width << '\t' << r.height << '\t' << r.fpsMilli / 1000.0 << '\t'
          << r.headerFrames << '\t' << r.frames << '\t' << r.videoEnd << '\t' << r.audioStreams << '\t'
          << r.audioRate << '\t' << r.audioChannels << '\t' << r.samples << '\t' << r.audioEnd << '\t' << r.mode
          << '\t' << r.effect << '\t' << r.seconds << '\t' << r.reason << '\n';
  }
  for (const auto& r : results)
    for (const auto& s : r.shots)
      std::cout << "shot " << s << '\n';
  std::cout << "movies=" << results.size() << " frames=" << frames << " video_seconds=" << videoSeconds
            << " with_audio=" << withAudio << " wall=" << wall << "s jobs=" << jobs << '\n';
  for (const auto& [status, count] : byStatus)
    std::cout << "  " << status << ": " << count << '\n';
  std::cout << "  sizes:";
  for (const auto& [size, count] : bySize)
    std::cout << ' ' << size << "=" << count;
  std::cout << '\n';
  return byStatus.count("FAIL") ? 1 : 0;
}
