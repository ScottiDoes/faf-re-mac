// Decodes every texture file of the real game data with the POSIX ImageCodec, the way the
// Vulkan backend asks for them, and checks each result:
//   create  - DeviceVulkan::CreateTexture: DecodeOptions{} (and skipMipLevels = 1, the
//             medium texture quality), result validated (layout, full mip chain, a format
//             vulkan::ToVulkanTexture maps, BC top level a multiple of 4);
//   direct  - when the DDS direct path took the file, the full decode must give the same bytes;
//   ui      - DeviceVulkan::GetTexture2D (the UI batch textures): BC3, one level,
//             ResizeFilter::None, plus GetImageSize;
// and dumps chosen samples to PNG (level 0, a lower level, the UI decode).
//
//   texture_corpus [--dump DIR] [--sample SUBSTRING]... [--threads N] [--list] <source>...
// A source is a zip archive (.scd .nx2 .faf .zip), a directory, or a .scmap file (its
// embedded DDS images). Entries are matched by extension: .dds .tga .png .bmp .jpg .jpeg.

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"
#include "platform/posix/gal/ImageDecode.h"

using gpg::gal::imaging::DecodeOptions;
using gpg::gal::imaging::FileFormat;
using gpg::gal::imaging::Image;
using gpg::gal::imaging::ImageCodec;
using gpg::gal::imaging::ResizeFilter;
namespace dxgi = gpg::gal::dxgi;
namespace fs = std::filesystem;

namespace
{
  struct Entry
  {
    std::string name;    // source!path
    std::string inner;   // path inside the source (lower case)
    std::vector<std::uint8_t> data;
  };

  std::string Lower(std::string text)
  {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
  }

  bool IsTexturePath(const std::string& lower)
  {
    for (const char* const extension : {".dds", ".tga", ".png", ".bmp", ".jpg", ".jpeg"}) {
      const std::size_t length = std::strlen(extension);
      if (lower.size() > length && lower.compare(lower.size() - length, length, extension) == 0) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::uint8_t> ReadFile(const fs::path& path)
  {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  }

  std::uint32_t U32(const std::uint8_t* const at)
  {
    return at[0] | (at[1] << 8U) | (at[2] << 16U) | (static_cast<std::uint32_t>(at[3]) << 24U);
  }

  std::uint32_t U16(const std::uint8_t* const at)
  {
    return at[0] | (at[1] << 8U);
  }

  /** The texture entries of a zip archive (stored or deflated). */
  bool ReadZip(const fs::path& path, std::vector<Entry>& out)
  {
    const std::vector<std::uint8_t> zip = ReadFile(path);
    if (zip.size() < 22U) {
      return false;
    }
    std::size_t end = zip.size() - 22U;
    while (end > 0U && U32(zip.data() + end) != 0x06054B50U) {
      --end;
    }
    if (U32(zip.data() + end) != 0x06054B50U) {
      return false;
    }
    const std::uint32_t count = U16(zip.data() + end + 10);
    std::size_t at = U32(zip.data() + end + 16);
    for (std::uint32_t index = 0U; index < count && at + 46U <= zip.size(); ++index) {
      if (U32(zip.data() + at) != 0x02014B50U) {
        return false;
      }
      const std::uint32_t method = U16(zip.data() + at + 10);
      const std::uint32_t compressed = U32(zip.data() + at + 20);
      const std::uint32_t size = U32(zip.data() + at + 24);
      const std::uint32_t nameLength = U16(zip.data() + at + 28);
      const std::uint32_t extraLength = U16(zip.data() + at + 30);
      const std::uint32_t commentLength = U16(zip.data() + at + 32);
      const std::uint32_t local = U32(zip.data() + at + 42);
      std::string name(reinterpret_cast<const char*>(zip.data() + at + 46), nameLength);
      at += 46U + nameLength + extraLength + commentLength;
      std::replace(name.begin(), name.end(), '\\', '/');
      const std::string lower = Lower(name);
      if (!IsTexturePath(lower) || local + 30U > zip.size()) {
        continue;
      }
      const std::size_t data = local + 30U + U16(zip.data() + local + 26) + U16(zip.data() + local + 28);
      if (data + compressed > zip.size()) {
        continue;
      }
      Entry entry;
      entry.name = path.filename().string() + "!" + name;
      entry.inner = lower;
      if (method == 0U) {
        entry.data.assign(zip.begin() + static_cast<std::ptrdiff_t>(data), zip.begin() + static_cast<std::ptrdiff_t>(data + size));
      } else if (method == 8U) {
        entry.data.resize(size);
        z_stream stream{};
        inflateInit2(&stream, -15);
        stream.next_in = const_cast<Bytef*>(zip.data() + data);
        stream.avail_in = compressed;
        stream.next_out = entry.data.data();
        stream.avail_out = size;
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result != Z_STREAM_END) {
          std::printf("inflate failed: %s\n", entry.name.c_str());
          continue;
        }
      } else {
        std::printf("unsupported zip method %u: %s\n", method, entry.name.c_str());
        continue;
      }
      out.push_back(std::move(entry));
    }
    return true;
  }

  /** The DDS images inside a .scmap: each is preceded by its byte count. */
  void ReadScmap(const fs::path& path, std::vector<Entry>& out)
  {
    const std::vector<std::uint8_t> map = ReadFile(path);
    int found = 0;
    for (std::size_t at = 4U; at + 128U <= map.size(); ++at) {
      if (map[at] != 'D' || std::memcmp(map.data() + at, "DDS ", 4) != 0 || U32(map.data() + at + 4) != 124U) {
        continue;
      }
      const std::uint32_t length = U32(map.data() + at - 4);
      if (length < 128U || at + length > map.size()) {
        continue;
      }
      Entry entry;
      entry.name = path.filename().string() + "!dds" + std::to_string(found++) + "@" + std::to_string(at);
      entry.inner = Lower(entry.name);
      entry.data.assign(map.begin() + static_cast<std::ptrdiff_t>(at), map.begin() + static_cast<std::ptrdiff_t>(at + length));
      out.push_back(std::move(entry));
      at += length - 1U;
    }
  }

  void Collect(const fs::path& source, std::vector<Entry>& out)
  {
    if (fs::is_directory(source)) {
      for (const auto& item : fs::recursive_directory_iterator(source)) {
        if (!item.is_regular_file()) {
          continue;
        }
        const std::string lower = Lower(item.path().string());
        if (lower.size() > 6U && lower.compare(lower.size() - 6U, 6U, ".scmap") == 0) {
          ReadScmap(item.path(), out);
        } else if (IsTexturePath(lower)) {
          Entry entry;
          entry.name = item.path().string();
          entry.inner = lower;
          entry.data = ReadFile(item.path());
          out.push_back(std::move(entry));
        }
      }
      return;
    }
    const std::string lower = Lower(source.string());
    if (lower.size() > 6U && lower.compare(lower.size() - 6U, 6U, ".scmap") == 0) {
      ReadScmap(source, out);
    } else if (!ReadZip(source, out)) {
      std::printf("not a zip archive: %s\n", source.string().c_str());
    }
  }

  std::string Kind(const Entry& entry)
  {
    const std::vector<std::uint8_t>& d = entry.data;
    if (d.size() >= 128U && std::memcmp(d.data(), "DDS ", 4) == 0) {
      const std::uint32_t flags = U32(d.data() + 8);
      const std::uint32_t height = U32(d.data() + 12);
      const std::uint32_t width = U32(d.data() + 16);
      std::uint32_t mips = (flags & 0x20000U) != 0U ? std::max(U32(d.data() + 28), 1U) : 1U;
      const std::uint32_t pfFlags = U32(d.data() + 80);
      const std::uint32_t caps2 = U32(d.data() + 112);
      std::uint32_t full = 1U;
      for (std::uint32_t side = std::max(width, height); side > 1U; side >>= 1U) {
        ++full;
      }
      char format[64];
      if ((pfFlags & 4U) != 0U) {
        const std::uint32_t code = U32(d.data() + 84);
        if (code < 256U) {
          std::snprintf(format, sizeof(format), "D3DFMT %u", code);
        } else {
          std::snprintf(format, sizeof(format), "%c%c%c%c", code & 0xFF, (code >> 8) & 0xFF, (code >> 16) & 0xFF, code >> 24);
        }
      } else {
        std::snprintf(format, sizeof(format), "%s%u", (pfFlags & 0x40U) != 0U ? "RGB" : ((pfFlags & 0x20000U) != 0U ? "L" : "A"), U32(d.data() + 88));
        if ((pfFlags & 1U) != 0U) {
          std::strncat(format, "+a", sizeof(format) - std::strlen(format) - 1U);
        }
        if (U32(d.data() + 88) == 16U) {
          char masks[24];
          std::snprintf(masks, sizeof(masks), "[%x]", U32(d.data() + 92));
          std::strncat(format, masks, sizeof(format) - std::strlen(format) - 1U);
        }
      }
      const bool pow2 = (width & (width - 1U)) == 0U && (height & (height - 1U)) == 0U;
      return std::string("dds ") + format + ((caps2 & 0x200U) != 0U ? " cube" : "") + (mips == full ? " mips=full" : (mips == 1U ? " mips=1" : " mips=partial")) +
        (pow2 ? "" : ((width % 4U == 0U && height % 4U == 0U) ? " npot4" : " npot"));
    }
    const std::size_t dot = entry.inner.rfind('.');
    return dot == std::string::npos ? std::string("?") : entry.inner.substr(dot + 1U);
  }

  bool VulkanTakes(const DXGI_FORMAT format)
  {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
      return true;
    default:
      return false;
    }
  }

  const char* FormatName(const DXGI_FORMAT format)
  {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      return "B8G8R8A8";
    case DXGI_FORMAT_B8G8R8X8_UNORM:
      return "B8G8R8X8";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
      return "R8G8B8A8";
    case DXGI_FORMAT_A8_UNORM:
      return "A8";
    case DXGI_FORMAT_R8_UNORM:
      return "R8";
    case DXGI_FORMAT_R8G8_UNORM:
      return "R8G8";
    case DXGI_FORMAT_BC1_UNORM:
      return "BC1";
    case DXGI_FORMAT_BC2_UNORM:
      return "BC2";
    case DXGI_FORMAT_BC3_UNORM:
      return "BC3";
    default:
      return "other";
    }
  }

  /** "" when the image is a well-formed result, else what is wrong. */
  std::string Validate(const Image& image, const bool fullChain)
  {
    if (!VulkanTakes(image.format)) {
      return "format " + std::to_string(static_cast<int>(image.format)) + " has no Vulkan mapping";
    }
    if (image.subresources.size() != static_cast<std::size_t>(image.mipLevels) * image.arraySize) {
      return "subresource count";
    }
    if (dxgi::IsBlockCompressed(image.format) && (image.width % 4U != 0U || image.height % 4U != 0U)) {
      return "BC top level not a multiple of 4";
    }
    std::uint32_t full = 1U;
    for (std::uint32_t side = std::max(image.width, image.height); side > 1U; side >>= 1U) {
      ++full;
    }
    if (fullChain && image.mipLevels != full) {
      return "mip chain " + std::to_string(image.mipLevels) + " of " + std::to_string(full);
    }
    std::size_t offset = 0U;
    for (std::uint32_t slice = 0U; slice < image.arraySize; ++slice) {
      for (std::uint32_t mip = 0U; mip < image.mipLevels; ++mip) {
        const Image::Subresource& sub = image.subresources[slice * image.mipLevels + mip];
        const dxgi::LevelLayout layout = dxgi::ComputeLevelLayout(image.format, std::max(1U, image.width >> mip), std::max(1U, image.height >> mip));
        if (sub.offset != offset || sub.rowPitch != layout.rowPitch || sub.slicePitch != layout.ByteSize()) {
          return "subresource layout";
        }
        offset += sub.slicePitch;
      }
    }
    if (offset != image.bytes.size()) {
      return "byte count";
    }
    if (image.cube && image.arraySize != 6U) {
      return "cube without 6 faces";
    }
    return {};
  }

  struct Stats
  {
    std::mutex mutex;
    std::map<std::string, int> kinds;          // input kind -> files
    std::map<std::string, int> outcomes;       // input kind -> output format / route
    std::map<std::string, double> routeMs;     // route -> total ms
    std::map<std::string, int> routeCount;
    std::vector<std::string> failures;
    std::vector<std::pair<double, std::string>> slowest;
    int directSame = 0;
    int directDiffer = 0;
    int uiDecodes = 0;
    double uiMs = 0.0;
    double totalMs = 0.0;
  };

  void Dump(const Image& image, const std::uint32_t index, const fs::path& path)
  {
    Image level;
    const Image::Subresource& sub = image.subresources[index];
    level.Allocate(image.format, sub.width, sub.height, 1U, 1U);
    std::memcpy(level.bytes.data(), image.bytes.data() + sub.offset, sub.slicePitch);
    std::vector<std::uint8_t> png;
    if (FAILED(ImageCodec::Instance()->Encode(level, FileFormat::Png, png))) {
      std::printf("cannot dump %s\n", path.string().c_str());
      return;
    }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  }

  std::string SafeName(std::string name)
  {
    for (char& c : name) {
      if (c == '/' || c == '!' || c == ' ' || c == '@' || c == '\\') {
        c = '_';
      }
    }
    return name;
  }

  void Process(const Entry& entry, Stats& stats, const fs::path& dumpDirectory, const std::vector<std::string>& samples)
  {
    ImageCodec* const codec = ImageCodec::Instance();
    const std::string kind = Kind(entry);
    const auto now = [] { return std::chrono::steady_clock::now(); };
    const auto ms = [](const auto begin, const auto end) { return std::chrono::duration<double, std::milli>(end - begin).count(); };
    std::vector<std::string> problems;

    // CreateTexture.
    Image image;
    auto begin = now();
    HRESULT result = codec->Decode(entry.data.data(), entry.data.size(), DecodeOptions{}, image);
    double elapsed = ms(begin, now());
    const std::string route = ImageCodec::LastDecodeRoute();
    std::string outcome = kind + " -> ";
    if (FAILED(result)) {
      char text[32];
      std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(result));
      problems.push_back(std::string("create: HRESULT ") + text);
      outcome += "FAILED";
    } else {
      const std::string invalid = Validate(image, !image.volume);
      if (!invalid.empty()) {
        problems.push_back("create: " + invalid);
      }
      outcome += std::string(FormatName(image.format)) + " (" + route + ")";
    }

    // The direct path must equal the full decode.
    int same = -1;
    if (SUCCEEDED(result) && route == "direct") {
      Image full;
      const HRESULT fullResult = faf_posix::gal::image::DecodeTexture(entry.data.data(), entry.data.size(), DecodeOptions{}, full);
      same = (SUCCEEDED(fullResult) && full.bytes == image.bytes && full.format == image.format && full.mipLevels == image.mipLevels) ? 1 : 0;
      if (same == 0) {
        problems.push_back("direct path differs from the full decode");
      }
    }

    // Medium texture quality: one level skipped.
    if (SUCCEEDED(result)) {
      DecodeOptions skip;
      skip.skipMipLevels = 1U;
      Image skipped;
      const HRESULT skipResult = codec->Decode(entry.data.data(), entry.data.size(), skip, skipped);
      if (FAILED(skipResult)) {
        problems.push_back("skip 1 failed");
      } else if (const std::string invalid = Validate(skipped, !skipped.volume); !invalid.empty()) {
        problems.push_back("skip 1: " + invalid);
      }
    }

    // GetTexture2D (UI batch textures): every 2D texture under textures/ui, and every non-DDS file.
    const bool ui = entry.inner.find("textures/ui/") != std::string::npos || kind.rfind("dds", 0) != 0;
    Image uiImage;
    double uiElapsed = 0.0;
    if (ui && SUCCEEDED(result) && !image.cube && !image.volume) {
      DecodeOptions options;
      options.format = DXGI_FORMAT_BC3_UNORM;
      options.mipLevels = 1U;
      options.filter = ResizeFilter::None;
      begin = now();
      const HRESULT uiResult = codec->Decode(entry.data.data(), entry.data.size(), options, uiImage);
      std::uint32_t width = 0U;
      std::uint32_t height = 0U;
      const HRESULT sizeResult = codec->GetImageSize(entry.data.data(), entry.data.size(), width, height);
      uiElapsed = ms(begin, now());
      if (FAILED(uiResult) || FAILED(sizeResult)) {
        problems.push_back("ui decode failed");
      } else if (uiImage.format != DXGI_FORMAT_BC3_UNORM || uiImage.mipLevels != 1U || uiImage.width != ((width + 3U) & ~3U) ||
                 uiImage.height != ((height + 3U) & ~3U)) {
        problems.push_back("ui shape " + std::to_string(uiImage.width) + "x" + std::to_string(uiImage.height) + " for " + std::to_string(width) + "x" +
                           std::to_string(height));
      }
    }

    // Samples.
    if (!dumpDirectory.empty() && SUCCEEDED(result)) {
      for (const std::string& sample : samples) {
        if (entry.inner.find(Lower(sample)) == std::string::npos) {
          continue;
        }
        const std::string base = SafeName(entry.inner);
        Dump(image, 0U, dumpDirectory / (base + ".level0.png"));
        if (image.mipLevels > 2U) {
          Dump(image, 2U, dumpDirectory / (base + ".level2.png"));
        }
        if (image.cube) {
          Dump(image, image.mipLevels * 2U, dumpDirectory / (base + ".face2.png"));
        }
        if (!uiImage.subresources.empty()) {
          Dump(uiImage, 0U, dumpDirectory / (base + ".ui.png"));
        }
        std::printf("sample %s: %s %ux%u, %u levels%s (%s)\n", entry.name.c_str(), FormatName(image.format), image.width, image.height,
                    image.mipLevels, image.cube ? ", cube" : "", route.c_str());
      }
    }

    const std::lock_guard<std::mutex> lock(stats.mutex);
    ++stats.kinds[kind];
    ++stats.outcomes[outcome];
    stats.routeMs[route] += elapsed;
    ++stats.routeCount[route];
    stats.totalMs += elapsed;
    if (uiElapsed > 0.0) {
      ++stats.uiDecodes;
      stats.uiMs += uiElapsed;
    }
    if (same == 1) {
      ++stats.directSame;
    } else if (same == 0) {
      ++stats.directDiffer;
    }
    for (const std::string& problem : problems) {
      stats.failures.push_back(entry.name + ": " + problem + " [" + kind + "]");
    }
    stats.slowest.emplace_back(elapsed, entry.name + " [" + kind + "] " + route);
    std::sort(stats.slowest.begin(), stats.slowest.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (stats.slowest.size() > 12U) {
      stats.slowest.resize(12U);
    }
  }
} // namespace

int main(int argc, char** argv)
{
  fs::path dumpDirectory;
  std::vector<std::string> samples;
  std::vector<fs::path> sources;
  unsigned threads = 8U;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--dump" && index + 1 < argc) {
      dumpDirectory = argv[++index];
    } else if (argument == "--sample" && index + 1 < argc) {
      samples.emplace_back(argv[++index]);
    } else if (argument == "--threads" && index + 1 < argc) {
      threads = static_cast<unsigned>(std::max(1, std::atoi(argv[++index])));
    } else {
      sources.emplace_back(argument);
    }
  }
  if (!dumpDirectory.empty()) {
    fs::create_directories(dumpDirectory);
  }
  gpg::gal::imaging::gTextureDecodeCache = std::getenv("FAF_TEST_CACHE_DIR") != nullptr;
  if (ImageCodec::Instance() == nullptr) {
    std::printf("no codec\n");
    return 2;
  }

  Stats stats;
  const auto started = std::chrono::steady_clock::now();
  std::size_t files = 0U;
  std::size_t bytes = 0U;
  for (const fs::path& source : sources) {
    std::vector<Entry> entries;
    Collect(source, entries);
    files += entries.size();
    for (const Entry& entry : entries) {
      bytes += entry.data.size();
    }
    std::atomic<std::size_t> next{0U};
    std::vector<std::thread> workers;
    for (unsigned worker = 0U; worker < threads; ++worker) {
      workers.emplace_back([&] {
        for (std::size_t index = next++; index < entries.size(); index = next++) {
          Process(entries[index], stats, dumpDirectory, samples);
        }
      });
    }
    for (std::thread& worker : workers) {
      worker.join();
    }
    std::printf("%-28s %6zu files\n", source.filename().string().c_str(), entries.size());
  }
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

  std::printf("\n== input kinds\n");
  for (const auto& [kind, count] : stats.kinds) {
    std::printf("%7d  %s\n", count, kind.c_str());
  }
  std::printf("\n== create decode: input -> output (route)\n");
  for (const auto& [outcome, count] : stats.outcomes) {
    std::printf("%7d  %s\n", count, outcome.c_str());
  }
  std::printf("\n== time per route (create decode, single file, summed over threads)\n");
  for (const auto& [route, total] : stats.routeMs) {
    std::printf("%-36s %7d files %10.1f ms  %.3f ms/file\n", route.c_str(), stats.routeCount[route], total, total / std::max(1, stats.routeCount[route]));
  }
  std::printf("ui decodes (BC3, 1 level, unscaled): %d files, %.1f ms, %.3f ms/file\n", stats.uiDecodes, stats.uiMs,
              stats.uiMs / std::max(1, stats.uiDecodes));
  std::printf("direct path vs full decode: %d identical, %d differ\n", stats.directSame, stats.directDiffer);
  std::printf("\n== slowest create decodes\n");
  for (const auto& [elapsed, name] : stats.slowest) {
    std::printf("%8.2f ms  %s\n", elapsed, name.c_str());
  }
  std::printf("\n== failures: %zu\n", stats.failures.size());
  std::sort(stats.failures.begin(), stats.failures.end());
  for (std::size_t index = 0U; index < stats.failures.size() && index < 60U; ++index) {
    std::printf("  %s\n", stats.failures[index].c_str());
  }
  std::printf("\n%zu files, %.1f MB, %.1f s wall on %u threads; create decodes %.1f s summed\n", files, static_cast<double>(bytes) / 1e6, wall, threads,
              stats.totalMs / 1000.0);
  return stats.failures.empty() ? 0 : 1;
}
