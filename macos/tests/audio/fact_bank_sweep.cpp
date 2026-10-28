// fact_bank_sweep: plays every cue of Supreme Commander: Forged Alliance's XACT banks through
// FAudio's FACT, one cue at a time, and measures the mixed output per cue.
//
// What it checks: that FACT (the XACT runtime the macOS build uses, src/platform/posix/audio)
// reads FA's XACT 2 content (SupCom.xgs, every .xwb and .xsb, content version 43) and that
// every cue produces sound. It loads the banks the way the game's AudioEngine does
// (moho/audio/AudioEngine.cpp): the global settings first, then every wave bank (in memory,
// or streaming with packet size 0x12 through read callbacks on a file descriptor, as the
// engine's streaming loader hands FACT a file handle), then every sound bank.
//
// Output: SDL3's audio driver (default "disk": the mixed output goes to --raw as 32-bit float
// stereo), and an engine-procedure tap (FAudio_SetEngineProcedureEXT) that attributes each
// mixed buffer to the cue that is playing. One CSV row per cue on stdout, a summary on stderr.
//
// Usage: fact_bank_sweep --xgs <SupCom.xgs> [--raw <file>] [--driver disk|dummy] [--timescale <x>]
//                        [--cap-ms <ms>] [--bank <substring>] [--max-cues-per-bank <n>]
//                        [--shard <i>/<n>] [--list] <dir> ...
// --timescale: SDL's disk/dummy drivers consume audio that many times faster than real time.
// --cap-ms: a cue still playing after this much wall time is stopped (loops, music).
// Build: macos/tests/audio/build.sh. Not part of the game; no engine code.

#include <FACT.h>
#include <FAudio.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
  struct CueStats
  {
    std::uint64_t frames = 0;
    std::uint64_t loudFrames = 0;   // a frame with any channel above -80 dBFS
    std::uint64_t clippedSamples = 0;
    double sumSquares = 0.0;        // over loud frames' samples
    float peak = 0.0f;
  };

  std::atomic<int> gCurrent{-1};  // index into gStats, -1 between cues
  std::vector<CueStats> gStats;   // sized before playback; the tap only writes gStats[gCurrent]
  std::atomic<std::uint64_t> gTapFrames{0};
  std::atomic<std::uint64_t> gTapSilentRun{0};  // consecutive silent frames
  std::uint32_t gChannels = 2;
  std::uint32_t gQuantumFrames = 480;

  constexpr float kSilence = 1.0e-4f;  // -80 dBFS

  void FAUDIOCALL Tap(FAudioEngineCallEXT defaultProc, FAudio* audio, float* output, void* user)
  {
    (void)user;
    defaultProc(audio, output);
    const int current = gCurrent.load(std::memory_order_acquire);
    CueStats* const stats = current >= 0 ? &gStats[static_cast<std::size_t>(current)] : nullptr;
    std::uint64_t silentRun = gTapSilentRun.load(std::memory_order_relaxed);
    for (std::uint32_t frame = 0; frame < gQuantumFrames; ++frame) {
      bool loud = false;
      for (std::uint32_t channel = 0; channel < gChannels; ++channel) {
        const float sample = output[frame * gChannels + channel];
        const float magnitude = std::fabs(sample);
        if (magnitude > kSilence) {
          loud = true;
        }
        if (stats != nullptr) {
          stats->peak = std::max(stats->peak, magnitude);
          if (magnitude >= 1.0f) {
            ++stats->clippedSamples;
          }
        }
      }
      if (stats != nullptr) {
        ++stats->frames;
        if (loud) {
          ++stats->loudFrames;
          for (std::uint32_t channel = 0; channel < gChannels; ++channel) {
            const double sample = output[frame * gChannels + channel];
            stats->sumSquares += sample * sample;
          }
        }
      }
      silentRun = loud ? 0 : silentRun + 1;
    }
    gTapSilentRun.store(silentRun, std::memory_order_relaxed);
    gTapFrames.fetch_add(gQuantumFrames, std::memory_order_release);
  }

  // --- File I/O callbacks: hFile is a FileRef*, offsets come in the overlapped block ------
  struct FileRef
  {
    int fd;
    std::atomic<std::uint32_t> highOffsets{0};
  };

  int32_t FACTCALL ReadFileCallback(
    void* hFile, void* buffer, uint32_t bytes, uint32_t* bytesRead, FACTOverlapped* overlapped
  )
  {
    auto* const file = static_cast<FileRef*>(hFile);
    if (overlapped->OffsetHigh != 0u) {
      file->highOffsets.fetch_add(1u);
    }
    const off_t offset = static_cast<off_t>(overlapped->Offset);
    std::size_t total = 0;
    while (total < bytes) {
      const ssize_t n = ::pread(file->fd, static_cast<char*>(buffer) + total, bytes - total, offset + total);
      if (n <= 0) {
        break;
      }
      total += static_cast<std::size_t>(n);
    }
    overlapped->Internal = nullptr;  // STATUS_SUCCESS
    overlapped->InternalHigh = reinterpret_cast<void*>(static_cast<std::uintptr_t>(total));
    if (bytesRead != nullptr) {
      *bytesRead = static_cast<uint32_t>(total);
    }
    return 1;
  }

  int32_t FACTCALL GetOverlappedResultCallback(void* hFile, FACTOverlapped* overlapped, uint32_t* transferred, int32_t wait)
  {
    (void)hFile;
    (void)wait;
    if (transferred != nullptr) {
      *transferred = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(overlapped->InternalHigh));
    }
    return 1;
  }

  std::atomic<int> gNotifications{0};
  void FACTCALL OnNotification(const FACTNotification* note)
  {
    gNotifications.fetch_add(1);
    if (note->type == FACTNOTIFICATIONTYPE_WAVEBANKSTREAMING_INVALIDCONTENT) {
      std::fprintf(stderr, "notification: invalid streaming content in wave bank %p\n", static_cast<void*>(note->waveBank.pWaveBank));
    }
  }

  std::vector<char> ReadAll(const fs::path& path)
  {
    std::vector<char> data;
    FILE* const f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
      return data;
    }
    std::fseek(f, 0, SEEK_END);
    data.resize(static_cast<std::size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (!data.empty() && std::fread(data.data(), 1, data.size(), f) != data.size()) {
      data.clear();
    }
    std::fclose(f);
    return data;
  }

  // The engine's IsStreamingWaveBank: bit 0 of the bank-data flags (segment 0).
  bool IsStreamingWaveBank(const std::vector<char>& head)
  {
    if (head.size() < 52) {
      return false;
    }
    std::uint32_t metadataOffset = 0;
    std::memcpy(&metadataOffset, head.data() + 12, 4);
    return metadataOffset + 4 <= head.size() && (static_cast<unsigned char>(head[metadataOffset]) & 1u) != 0;
  }

  double NowMs()
  {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
  }

  void SleepMs(double ms)
  {
    std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(ms * 1000.0)));
  }

  struct Args
  {
    std::string xgs;
    std::string raw = "fact_bank_sweep.f32";
    std::string driver = "disk";
    double timescale = 1.0;
    double capMs = 4000.0;
    std::string bankFilter;
    int maxCuesPerBank = 0;
    int shard = 0;       // --shard i/n: play only the sound banks with index % n == i
    int shardCount = 1;
    bool list = false;
    std::vector<std::string> dirs;
  };
} // namespace

int main(int argc, char** argv)
{
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--xgs") args.xgs = next();
    else if (a == "--raw") args.raw = next();
    else if (a == "--driver") args.driver = next();
    else if (a == "--timescale") args.timescale = std::atof(next().c_str());
    else if (a == "--cap-ms") args.capMs = std::atof(next().c_str());
    else if (a == "--bank") args.bankFilter = next();
    else if (a == "--max-cues-per-bank") args.maxCuesPerBank = std::atoi(next().c_str());
    else if (a == "--shard") {
      const std::string v = next();
      std::sscanf(v.c_str(), "%d/%d", &args.shard, &args.shardCount);
      if (args.shardCount < 1) args.shardCount = 1;
    }
    else if (a == "--list") args.list = true;
    else args.dirs.push_back(a);
  }
  if (args.xgs.empty() || args.dirs.empty()) {
    std::fprintf(stderr, "usage: fact_bank_sweep --xgs <SupCom.xgs> [options] <sound dir> ...\n");
    return 2;
  }

  // SDL reads its hints from the environment; FAudio initialises SDL audio itself.
  const std::string timescale = std::to_string(args.timescale);
  setenv("SDL_AUDIO_DRIVER", args.driver.c_str(), 1);
  setenv("SDL_AUDIO_DISK_OUTPUT_FILE", args.raw.c_str(), 1);
  setenv("SDL_AUDIO_DISK_TIMESCALE", timescale.c_str(), 1);
  setenv("SDL_AUDIO_DUMMY_TIMESCALE", timescale.c_str(), 1);
  setenv("SDL_AUDIO_FORMAT", "F32", 1);
  setenv("SDL_AUDIO_FREQUENCY", "48000", 1);
  setenv("SDL_AUDIO_CHANNELS", "2", 1);

  std::vector<fs::path> waveBanks;
  std::vector<fs::path> soundBanks;
  for (const std::string& dir : args.dirs) {
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".xwb") waveBanks.push_back(entry.path());
      if (ext == ".xsb") soundBanks.push_back(entry.path());
    }
  }
  std::sort(waveBanks.begin(), waveBanks.end());
  std::sort(soundBanks.begin(), soundBanks.end());

  std::vector<char> settings = ReadAll(args.xgs);
  if (settings.empty()) {
    std::fprintf(stderr, "cannot read %s\n", args.xgs.c_str());
    return 1;
  }

  FAudio* audio = nullptr;
  if (FAudioCreate(&audio, 0, FAUDIO_DEFAULT_PROCESSOR) != 0) {
    std::fprintf(stderr, "FAudioCreate failed\n");
    return 1;
  }
  FAudioMasteringVoice* master = nullptr;
  if (FAudio_CreateMasteringVoice(audio, &master, FAUDIO_DEFAULT_CHANNELS, FAUDIO_DEFAULT_SAMPLERATE, 0, 0, nullptr) != 0) {
    std::fprintf(stderr, "FAudio_CreateMasteringVoice failed (driver %s)\n", args.driver.c_str());
    return 1;
  }
  FAudioVoiceDetails details{};
  FAudioVoice_GetVoiceDetails(master, &details);
  gChannels = details.InputChannels;
  gQuantumFrames = details.InputSampleRate / 100;  // FAudio's SDL3 platform: 10 ms quanta
  std::fprintf(stderr, "mastering voice: %u channels, %u Hz, quantum %u frames, driver %s\n",
               details.InputChannels, details.InputSampleRate, gQuantumFrames, SDL_GetCurrentAudioDriver());
  FAudio_SetEngineProcedureEXT(audio, Tap, nullptr);

  FACTAudioEngine* engine = nullptr;
  if (FACTCreateEngine(0, &engine) != 0) {
    std::fprintf(stderr, "FACTCreateEngine failed\n");
    return 1;
  }
  FACTRuntimeParameters params{};
  params.lookAheadTime = 250;
  params.pGlobalSettingsBuffer = settings.data();
  params.globalSettingsBufferSize = static_cast<uint32_t>(settings.size());
  params.fileIOCallbacks.readFileCallback = ReadFileCallback;
  params.fileIOCallbacks.getOverlappedResultCallback = GetOverlappedResultCallback;
  params.fnNotificationCallback = OnNotification;
  params.pXAudio2 = audio;
  params.pMasteringVoice = master;
  const uint32_t init = FACTAudioEngine_Initialize(engine, &params);
  std::fprintf(stderr, "FACTAudioEngine_Initialize(%s) = 0x%08x\n", args.xgs.c_str(), init);
  if (init != 0) {
    return 1;
  }
  FACTNotificationDescription persistInvalid{};
  persistInvalid.type = FACTNOTIFICATIONTYPE_WAVEBANKSTREAMING_INVALIDCONTENT;
  persistInvalid.flags = FACT_FLAG_NOTIFICATION_PERSIST;
  FACTAudioEngine_RegisterNotification(engine, &persistInvalid);

  std::vector<FileRef*> files;
  int waveFailures = 0;
  for (const fs::path& path : waveBanks) {
    std::vector<char> head(512);
    {
      FILE* f = std::fopen(path.c_str(), "rb");
      head.resize(f != nullptr ? std::fread(head.data(), 1, head.size(), f) : 0);
      if (f != nullptr) std::fclose(f);
    }
    const bool streaming = IsStreamingWaveBank(head);
    FACTWaveBank* bank = nullptr;
    uint32_t result = 0;
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
      std::fprintf(stderr, "WAVEBANK %s: cannot open\n", path.c_str());
      ++waveFailures;
      continue;
    }
    if (streaming) {
      auto* const file = new FileRef{fd};
      files.push_back(file);
      FACTStreamingParameters streamingParams{};
      streamingParams.file = file;
      streamingParams.offset = 0;
      streamingParams.flags = 0;
      streamingParams.packetSize = 0x12;
      result = FACTAudioEngine_CreateStreamingWaveBank(engine, &streamingParams, &bank);
    } else {
      struct stat info{};
      ::fstat(fd, &info);
      void* const mapped = ::mmap(nullptr, static_cast<std::size_t>(info.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
      ::close(fd);
      if (mapped == MAP_FAILED) {
        std::fprintf(stderr, "WAVEBANK %s: mmap failed\n", path.c_str());
        ++waveFailures;
        continue;
      }
      result = FACTAudioEngine_CreateInMemoryWaveBank(engine, mapped, static_cast<uint32_t>(info.st_size), 0, 0, &bank);
    }
    uint16_t waves = 0;
    if (result == 0 && bank != nullptr) {
      FACTWaveBank_GetNumWaves(bank, &waves);
    } else {
      ++waveFailures;
    }
    std::fprintf(stderr, "WAVEBANK %-40s %-9s result 0x%08x waves %u\n", path.filename().c_str(),
                 streaming ? "streaming" : "in-memory", result, waves);
  }

  struct LoadedBank
  {
    fs::path path;
    FACTSoundBank* bank;
    std::vector<char> data;
  };
  std::vector<LoadedBank*> banks;
  int soundFailures = 0;
  std::size_t totalCues = 0;
  for (const fs::path& path : soundBanks) {
    auto* const loaded = new LoadedBank{path, nullptr, ReadAll(path)};
    const uint32_t result = FACTAudioEngine_CreateSoundBank(
      engine, loaded->data.data(), static_cast<uint32_t>(loaded->data.size()), 0, 0, &loaded->bank
    );
    uint16_t cues = 0;
    if (result == 0 && loaded->bank != nullptr) {
      FACTSoundBank_GetNumCues(loaded->bank, &cues);
      banks.push_back(loaded);
      totalCues += cues;
    } else {
      ++soundFailures;
    }
    std::fprintf(stderr, "SOUNDBANK %-40s result 0x%08x cues %u\n", path.filename().c_str(), result, cues);
  }
  std::fprintf(stderr, "loaded %zu wave banks (%d failed), %zu sound banks (%d failed), %zu cues\n",
               waveBanks.size() - waveFailures, waveFailures, banks.size(), soundFailures, totalCues);
  if (args.list) {
    for (LoadedBank* loaded : banks) {
      uint16_t cues = 0;
      FACTSoundBank_GetNumCues(loaded->bank, &cues);
      for (uint16_t i = 0; i < cues; ++i) {
        FACTCueProperties props{};
        FACTSoundBank_GetCueProperties(loaded->bank, i, &props);
        std::printf("%s,%u,%s,variations=%u,interactive=%d\n", loaded->path.filename().c_str(), i,
                    props.friendlyName, props.numVariations, props.interactive);
      }
    }
    std::fflush(stdout);
    std::_Exit(0);
  }

  // Playback: one cue at a time, DoWork every 5 ms as the game's frame loop would.
  gStats.resize(totalCues + 1);
  std::printf("bank,cue,name,play_result,end_state,ended,wall_ms,frames,loud_frames,peak,rms_dbfs,clipped\n");
  int index = 0;
  int audible = 0;
  int silent = 0;
  int playErrors = 0;
  const double startWall = NowMs();
  for (std::size_t bankIndex = 0; bankIndex < banks.size(); ++bankIndex) {
    LoadedBank* const loaded = banks[bankIndex];
    if (static_cast<int>(bankIndex % static_cast<std::size_t>(args.shardCount)) != args.shard) {
      uint16_t skipped = 0;
      FACTSoundBank_GetNumCues(loaded->bank, &skipped);
      index += skipped;
      continue;
    }
    if (!args.bankFilter.empty() && loaded->path.filename().string().find(args.bankFilter) == std::string::npos) {
      continue;
    }
    uint16_t cues = 0;
    FACTSoundBank_GetNumCues(loaded->bank, &cues);
    const int limit = args.maxCuesPerBank > 0 ? std::min<int>(cues, args.maxCuesPerBank) : cues;
    for (int cueIndex = 0; cueIndex < limit; ++cueIndex, ++index) {
      FACTCueProperties props{};
      FACTSoundBank_GetCueProperties(loaded->bank, static_cast<uint16_t>(cueIndex), &props);

      // Let the previous cue's tail die away (or 400 ms of wall time pass).
      const double gapStart = NowMs();
      while (gTapSilentRun.load() < gQuantumFrames * 5 && NowMs() - gapStart < 400.0) {
        FACTAudioEngine_DoWork(engine);
        SleepMs(5.0);
      }

      gCurrent.store(index, std::memory_order_release);
      const double t0 = NowMs();
      FACTCue* cue = nullptr;
      const uint32_t playResult = FACTSoundBank_Play(loaded->bank, static_cast<uint16_t>(cueIndex), 0, 0, &cue);
      uint32_t state = 0;
      bool ended = false;
      if (playResult == 0 && cue != nullptr) {
        while (NowMs() - t0 < args.capMs) {
          FACTAudioEngine_DoWork(engine);
          FACTCue_GetState(cue, &state);
          if ((state & FACT_STATE_STOPPED) != 0u) {
            ended = true;
            break;
          }
          SleepMs(5.0);
        }
        if (!ended) {
          FACTCue_Stop(cue, FACT_FLAG_STOP_IMMEDIATE);
          for (int i = 0; i < 40; ++i) {
            FACTAudioEngine_DoWork(engine);
            uint32_t afterStop = 0;
            FACTCue_GetState(cue, &afterStop);
            if ((afterStop & FACT_STATE_STOPPED) != 0u) break;
            SleepMs(5.0);
          }
        }
        // The tail: keep attributing output for 60 ms of audio after the end.
        const std::uint64_t until = gTapFrames.load() + gQuantumFrames * 6;
        const double tailStart = NowMs();
        while (gTapFrames.load() < until && NowMs() - tailStart < 500.0) {
          SleepMs(2.0);
        }
        FACTCue_Destroy(cue);
      }
      gCurrent.store(-1, std::memory_order_release);
      const double wallMs = NowMs() - t0;

      const CueStats& s = gStats[static_cast<std::size_t>(index)];
      const double rms = s.loudFrames > 0 ? std::sqrt(s.sumSquares / (static_cast<double>(s.loudFrames) * gChannels)) : 0.0;
      const double rmsDb = rms > 0.0 ? 20.0 * std::log10(rms) : -999.0;
      const bool isAudible = s.loudFrames > 0;
      if (playResult != 0 || cue == nullptr) {
        ++playErrors;
      } else if (isAudible) {
        ++audible;
      } else {
        ++silent;
      }
      std::printf("%s,%d,%s,0x%08x,0x%08x,%s,%.0f,%llu,%llu,%.4f,%.1f,%llu\n", loaded->path.filename().c_str(), cueIndex,
                  props.friendlyName, playResult, state, ended ? "natural" : "capped", wallMs,
                  static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.loudFrames), s.peak, rmsDb,
                  static_cast<unsigned long long>(s.clippedSamples));
      std::fflush(stdout);
    }
  }
  std::uint32_t highOffsets = 0;
  for (FileRef* file : files) highOffsets += file->highOffsets.load();
  std::fprintf(stderr,
               "played %d cues in %.0f s: %d audible, %d silent, %d play errors; %d notifications; "
               "%u reads with OffsetHigh != 0; tap frames %llu\n",
               audible + silent + playErrors, (NowMs() - startWall) / 1000.0, audible, silent, playErrors, gNotifications.load(), highOffsets,
               static_cast<unsigned long long>(gTapFrames.load()));
  std::fflush(stdout);
  std::fflush(stderr);
  // Stop the mixer and close SDL's audio (the disk driver flushes and closes its file), then
  // exit without FACT's shutdown (FACT was handed our FAudio and mastering voice).
  FAudio_StopEngine(audio);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  std::_Exit(0);
}
