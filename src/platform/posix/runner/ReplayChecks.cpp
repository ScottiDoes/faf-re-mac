// The replay's recorded player checksums, for the result line's recorded= and checked=
// (how many checksums the run really compared), counted without touching the sim.
//
// Every player's client records, every sim_ChecksumPeriod beats, its own digest of a beat as a
// CMDST_VerifyChecksum command (op 3, 23 bytes: the 3-byte message header, a 16-byte MD5 digest,
// the int32 beat). Playing the replay, the sim compares them in Sim::VerifyChecksum
// (moho/sim/Sim.cpp:7941) and logs only the mismatches; a match leaves no trace. So the runner
// reads the same command stream the engine reads and applies the engine's rules to it:
//
//  - CReplayClient::Start (moho/net/CReplayClient.cpp:218-249) forwards a command only while
//    the last CMDST_SetCommandSource named one of the header's command sources (none at first).
//  - Sim::VerifyChecksum ignores a record while the sim has no valid command source, and
//    compares it only when its beat lies in [curBeat - 128, curBeat): the sim keeps 128 beat
//    digests, and a beat it has not run yet is "in the future".
//  - curBeat counts Sim::Sync calls. CSimDriver runs one opening Sync before the first command
//    (SimDriver.cpp:1150, the digest of beat 0), then per beat the beat's commands, its
//    CMDST_Advance (Sim::AdvanceBeat) and one Sync (SimDriver.cpp:830-906; Sim.cpp:5806). So a
//    record after the k-th CMDST_Advance of the stream is decoded at curBeat = k + 1.
//  - Every Sync makes one sync packet, and CWldSession::DoBeat counts the packets the user side
//    applied (gUserSyncBeats, the runner's beats + 1). Once the user side applied N of them, the
//    sim had run N Syncs, so it had decoded every record with curBeat < N.
//
// checked=C is therefore the number of records the sim compared by the time the user side
// applied the last packet it saw: a lower bound while the run is cut short, all of them once
// the replay played to its end. macos/tests/replay/replay_tool.py implements the same rules
// independently (its `checked` and `info` commands), and macos/verify_replays.sh compares the
// two. The digests themselves are not looked at here: mismatches= counts what the sim logged.

#include "platform/posix/runner/Runner.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
  constexpr std::uint8_t kOpAdvance = 0;
  constexpr std::uint8_t kOpSetCommandSource = 1;
  constexpr std::uint8_t kOpVerifyChecksum = 3;
  constexpr std::size_t kMessageHeaderBytes = 3;
  constexpr std::size_t kVerifyChecksumBytes = kMessageHeaderBytes + 16 + 4;
  constexpr std::int64_t kSimHashHistory = 128;  // Sim::mSimHashes
  constexpr char kSignature[] = "Replay v1.9\r\n";
  constexpr std::size_t kMaxReplayBytes = 512u * 1024u * 1024u;

  // curBeat of every record the sim compares when the whole stream plays, in stream order
  // (so never decreasing). Filled once by ReplayChecksLoad before the run starts, then only
  // read, also by the crash handler (a binary search: no allocation, no lock).
  std::vector<std::int64_t> gComparedAt;
  std::int64_t gRecorded = -1;
  std::int64_t gStreamBeats = -1;
  std::atomic<bool> gLoaded{false};

  class ByteReader
  {
  public:
    ByteReader(const std::uint8_t* const data, const std::size_t size)
      : mData(data)
      , mSize(size)
    {}

    [[nodiscard]] bool CString()
    {
      const void* const end = std::memchr(mData + mPos, 0, mSize - mPos);
      if (end == nullptr) {
        return false;
      }
      mPos = static_cast<std::size_t>(static_cast<const std::uint8_t*>(end) - mData) + 1u;
      return true;
    }

    [[nodiscard]] bool Skip(const std::size_t count)
    {
      if (count > mSize - mPos) {
        return false;
      }
      mPos += count;
      return true;
    }

    [[nodiscard]] bool U8(std::uint8_t* const out)
    {
      if (mPos >= mSize) {
        return false;
      }
      *out = mData[mPos++];
      return true;
    }

    [[nodiscard]] bool U32(std::uint32_t* const out)
    {
      if (mSize - mPos < 4u) {
        return false;
      }
      *out = static_cast<std::uint32_t>(mData[mPos]) | (static_cast<std::uint32_t>(mData[mPos + 1]) << 8) |
             (static_cast<std::uint32_t>(mData[mPos + 2]) << 16) | (static_cast<std::uint32_t>(mData[mPos + 3]) << 24);
      mPos += 4u;
      return true;
    }

    [[nodiscard]] bool LengthPrefixed()
    {
      std::uint32_t length = 0;
      return U32(&length) && Skip(length);
    }

    [[nodiscard]] bool Matches(const char* const text, const std::size_t count) const
    {
      return mSize - mPos >= count && std::memcmp(mData + mPos, text, count) == 0;
    }

    [[nodiscard]] std::size_t Position() const
    {
      return mPos;
    }

  private:
    const std::uint8_t* mData;
    std::size_t mSize;
    std::size_t mPos = 0;
  };

  [[nodiscard]] std::int32_t ReadInt32(const std::uint8_t* const p)
  {
    const std::uint32_t value = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                                (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    return static_cast<std::int32_t>(value);
  }

  // SessionStartup.cpp VCR_SetupReplaySession's header, in its order. On success the reader
  // stands at the first command and `sourceCount` holds the header's command-source count.
  [[nodiscard]] bool SkipHeader(ByteReader& reader, std::uint32_t* const sourceCount, std::string* const error)
  {
    const auto fail = [error](const char* const what) {
      *error = what;
      return false;
    };
    if (!reader.CString() || !reader.CString()) {
      return fail("the header ends early");
    }
    if (!reader.Matches(kSignature, sizeof(kSignature) - 1u)) {
      return fail("not a Forged Alliance replay (no \"Replay v1.9\" signature)");
    }
    if (!reader.Skip(sizeof(kSignature) - 1u) || !reader.CString() || !reader.CString() ||
        !reader.LengthPrefixed() || !reader.LengthPrefixed()) {
      return fail("the header ends early");
    }
    std::uint8_t sources = 0;
    if (!reader.U8(&sources)) {
      return fail("the header ends early");
    }
    for (std::uint32_t i = 0; i < sources; ++i) {
      std::uint32_t timeouts = 0;
      if (!reader.CString() || !reader.U32(&timeouts)) {
        return fail("the header ends early");
      }
    }
    std::uint8_t cheats = 0;
    std::uint8_t armies = 0;
    if (!reader.U8(&cheats) || !reader.U8(&armies)) {
      return fail("the header ends early");
    }
    for (std::uint32_t i = 0; i < armies; ++i) {
      if (!reader.LengthPrefixed()) {
        return fail("the header ends early");
      }
      std::uint8_t source = 0;
      do {
        if (!reader.U8(&source)) {
          return fail("the header ends early");
        }
      } while (source != 0xFFu);
    }
    std::uint32_t seed = 0;
    if (!reader.U32(&seed)) {
      return fail("the header ends early");
    }
    *sourceCount = sources;
    return true;
  }
} // namespace

bool faf_runner::ReplayChecksParse(
  const std::uint8_t* const data, const std::size_t size, ReplayChecksSummary* const out, std::string* const error
)
{
  ByteReader reader(data, size);
  std::uint32_t sourceCount = 0;
  if (!SkipHeader(reader, &sourceCount, error)) {
    return false;
  }

  ReplayChecksSummary summary{};
  bool forwarding = false;     // CReplayClient::mCurrentSourceAllowed
  bool simSourceValid = false; // Sim::mCurCommandSource != invalid
  std::int64_t advances = 0;
  std::size_t pos = reader.Position();
  // CMessage::Read: [op u8][size u16 LE, header included][payload]. A size below 3 stops the
  // engine's reader for good; a cut-off last message is never completed.
  while (size - pos >= kMessageHeaderBytes) {
    const std::uint8_t op = data[pos];
    const std::size_t messageBytes = static_cast<std::size_t>(data[pos + 1]) | (static_cast<std::size_t>(data[pos + 2]) << 8);
    if (messageBytes < kMessageHeaderBytes || messageBytes > size - pos) {
      summary.truncated = true;
      break;
    }
    const std::uint8_t* const payload = data + pos + kMessageHeaderBytes;
    if (op == kOpSetCommandSource) {
      forwarding = messageBytes > kMessageHeaderBytes && payload[0] < sourceCount;
      if (forwarding) {
        simSourceValid = true;  // Sim::SetCommandSource accepts any source below the count
      }
    } else if (op == kOpAdvance) {
      ++advances;
    } else if (op == kOpVerifyChecksum && messageBytes >= kVerifyChecksumBytes) {
      // CDecoder::DecodeVerifyChecksum reads the digest and the beat; later bytes are unread.
      ++summary.recorded;
      const std::int64_t beat = ReadInt32(payload + 16);
      const std::int64_t curBeat = advances + 1;
      if (forwarding && simSourceValid && beat < curBeat && beat >= curBeat - kSimHashHistory) {
        summary.comparedAt.push_back(curBeat);
      }
    }
    pos += messageBytes;
  }
  summary.streamBeats = advances;
  *out = std::move(summary);
  return true;
}

bool faf_runner::ReplayChecksLoad(const char* const path, std::string* const error)
{
  std::FILE* const file = (path != nullptr && path[0] != '\0') ? std::fopen(path, "rb") : nullptr;
  if (file == nullptr) {
    *error = "cannot open the replay file";
    return false;
  }
  std::vector<std::uint8_t> data;
  std::uint8_t chunk[65536];
  std::size_t got = 0;
  while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0 && data.size() <= kMaxReplayBytes) {
    data.insert(data.end(), chunk, chunk + got);
  }
  const bool readError = std::ferror(file) != 0;
  std::fclose(file);
  if (readError || data.size() > kMaxReplayBytes) {
    *error = readError ? "cannot read the replay file" : "the replay file is too large";
    return false;
  }

  ReplayChecksSummary summary{};
  if (!ReplayChecksParse(data.data(), data.size(), &summary, error)) {
    return false;
  }
  gComparedAt = std::move(summary.comparedAt);
  gRecorded = summary.recorded;
  gStreamBeats = summary.streamBeats;
  gLoaded.store(true);
  return true;
}

std::int64_t faf_runner::ReplayChecksRecorded()
{
  return gLoaded.load() ? gRecorded : -1;
}

std::int64_t faf_runner::ReplayChecksComparable()
{
  return gLoaded.load() ? static_cast<std::int64_t>(gComparedAt.size()) : -1;
}

std::int64_t faf_runner::ReplayChecksStreamBeats()
{
  return gLoaded.load() ? gStreamBeats : -1;
}

std::int64_t faf_runner::ReplayChecksCompared(const std::uint64_t appliedSyncPackets)
{
  if (!gLoaded.load()) {
    return -1;
  }
  // Records decoded at curBeat < N.
  const auto limit = static_cast<std::int64_t>(std::min<std::uint64_t>(appliedSyncPackets, INT64_MAX));
  const auto end = std::lower_bound(gComparedAt.begin(), gComparedAt.end(), limit);
  return static_cast<std::int64_t>(end - gComparedAt.begin());
}
