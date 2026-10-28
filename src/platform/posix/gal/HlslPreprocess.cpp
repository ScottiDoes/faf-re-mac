// D3DPreprocess's job on POSIX (contract: HlslPreprocess.h), done by DXC's preprocessor
// (clang's, `-P`) through the DXC bridge (platform/posix/dxc). Notes and the comparison
// with d3dcompiler_47's output: macos/porting/shaders.md.
//
// What d3d11::TranslateEffectSource and the effect layer need from the text, and how DXC
// gives it:
//   - every macro expanded, everywhere (technique blocks, pass states, annotations,
//     `compile vs_2_0 F()` -> `compile vs_4_0 F()` through the injected macros);
//     token pasting expands its operands first as fxc does (-flegacy-macro-expansion);
//   - conditionals resolved, comments removed, string literals untouched;
//   - no directive lines other than `#line` (and pass-through `#pragma`): the
//     translator drops every line that starts with '#';
//   - an #include fails, as with D3DPreprocess and no ID3DInclude (no include handler).
// Diagnostics come back in fxc's form, "<name>(<line>,<col>): error: <message>", one
// per line, without clang's source excerpts.

#include "HlslPreprocess.h"

#include <string>

#include "platform/posix/dxc/DxcBridge.h"

namespace faf_posix::gal
{
  namespace
  {
    // The name DXC is given for the source. The engine's names are VFS paths
    // ("/effects/mesh.fx") that DXC would read as options, so a fixed name goes in and
    // the real one is put back into the diagnostics and #line directives.
    constexpr std::string_view kSourceName = "faf_effect_source.fx";

    [[nodiscard]] bool IsDigits(const std::string_view text) noexcept
    {
      if (text.empty()) {
        return false;
      }
      for (const char c : text) {
        if (c < '0' || c > '9') {
          return false;
        }
      }
      return true;
    }

    /**
     * "faf_effect_source.fx:12:5: error: msg" -> "<name>(12,5): error: msg" (other files,
     * such as "<built-in>" for the defines, keep their name). Lines that are not
     * diagnostics (clang's source excerpt and caret) are dropped; diagnostics without a
     * location keep their text.
     */
    [[nodiscard]] std::string WindowsStyleDiagnostics(const std::string_view messages, const std::string_view name)
    {
      std::string out;
      std::size_t start = 0U;
      while (start < messages.size()) {
        std::size_t end = messages.find('\n', start);
        if (end == std::string_view::npos) {
          end = messages.size();
        }
        std::string_view line = messages.substr(start, end - start);
        start = end + 1U;
        if (!line.empty() && line.back() == '\r') {
          line.remove_suffix(1U);
        }

        // <file>:<line>:<col>: <rest>, the file being the source or "<built-in>" (-D).
        if (const std::size_t fileEnd = line.find(':'); fileEnd != std::string_view::npos && fileEnd != 0U) {
          const std::string_view file = line.substr(0U, fileEnd);
          const std::string_view located = line.substr(fileEnd + 1U);
          const std::size_t lineEnd = located.find(':');
          const std::size_t columnEnd = (lineEnd == std::string_view::npos) ? lineEnd : located.find(':', lineEnd + 1U);
          if (columnEnd != std::string_view::npos && IsDigits(located.substr(0U, lineEnd)) &&
              IsDigits(located.substr(lineEnd + 1U, columnEnd - lineEnd - 1U))) {
            out.append(file == kSourceName ? name : file);
            out += '(';
            out.append(located.substr(0U, lineEnd));
            out += ',';
            out.append(located.substr(lineEnd + 1U, columnEnd - lineEnd - 1U));
            out += "):";
            out.append(located.substr(columnEnd + 1U));
            out += '\n';
            continue;
          }
        }
        // Location-less diagnostics ("error: ...", "fatal error: ...") are kept; the
        // excerpt and caret lines under a located one are not.
        if (line.rfind("error:", 0U) == 0U || line.rfind("fatal error:", 0U) == 0U || line.rfind("warning:", 0U) == 0U) {
          out.append(line);
          out += '\n';
        }
      }
      return out;
    }

    /** `#line <n> "faf_effect_source.fx"` -> `#line <n> "<name>"`, as D3DPreprocess names the source. */
    void RenameLineDirectives(std::string& text, const std::string_view name)
    {
      std::string quotedPlaceholder = "\"";
      quotedPlaceholder.append(kSourceName);
      quotedPlaceholder += '"';
      std::string quotedName = "\"";
      for (const char c : name) {
        if (c == '\\' || c == '"') {
          quotedName += '\\';
        }
        quotedName += c;
      }
      quotedName += '"';

      std::size_t position = 0U;
      while ((position = text.find(quotedPlaceholder, position)) != std::string::npos) {
        const std::size_t lineStart = text.rfind('\n', position);
        const std::size_t first = (lineStart == std::string::npos) ? 0U : lineStart + 1U;
        if (text.compare(first, 5U, "#line") == 0) {
          text.replace(position, quotedPlaceholder.size(), quotedName);
          position += quotedName.size();
        } else {
          position += quotedPlaceholder.size();
        }
      }
    }
  } // namespace

  PreprocessedEffect PreprocessEffect(
    const std::string_view source, const char* const sourceName, const std::vector<std::pair<std::string, std::string>>& defines
  )
  {
    const std::string_view name = (sourceName != nullptr) ? std::string_view(sourceName) : std::string_view("<effect>");

    std::vector<std::wstring> arguments{L"-P", L"-flegacy-macro-expansion"};
    arguments.reserve(arguments.size() + 2U * defines.size() + 1U);
    for (const auto& [macro, value] : defines) {
      arguments.emplace_back(L"-D");
      arguments.push_back(faf_posix::dxc::Widen(macro + "=" + value));
    }
    arguments.push_back(faf_posix::dxc::Widen(kSourceName));

    faf_posix::dxc::CompileOutput output = faf_posix::dxc::Compile(source, arguments);
    PreprocessedEffect result;
    result.errors = WindowsStyleDiagnostics(output.messages, name);
    if (!output.succeeded) {
      if (result.errors.empty()) {
        // Not a diagnostic (DXC not loaded, Compile failed): pass the bridge's text on.
        result.errors = !output.messages.empty() ? output.messages : std::string("preprocessing failed");
      }
      return result;
    }
    result.text = std::move(output.text);
    RenameLineDirectives(result.text, name);
    result.succeeded = true;
    return result;
  }
} // namespace faf_posix::gal
