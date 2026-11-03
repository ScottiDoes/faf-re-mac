#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Where a Lua state's memory lives on 64-bit builds (not in the binary).
 *
 * A Lua value is 8 bytes in the 2007 binary, a tag and a 32-bit pointer, which is what keeps tables
 * small. `LuaPlus::ValuePointer` keeps it 8 bytes on x64: every Lua object comes from this one range
 * below 4 GB, so its pointer fits 32 bits and reads back with a zero extend. `nblocks` keeps counting
 * x86 sizes (LuaMemAccounting.h), so the layout doesn't change how often Lua collects.
 *
 * 32-bit builds keep the binary's allocator and none of this exists.
 */
#if INTPTR_MAX != INT32_MAX
namespace lua
{
  /** Reserves the range, once. Allocating does it too; FAF_HIGHMEM has to do it before it takes the rest. */
  void ArenaReserve() noexcept;

  /** A block of at least `bytes`, 8-byte aligned, from the range. Null when the range is full. */
  [[nodiscard]] void* ArenaAllocate(std::size_t bytes) noexcept;

  /** The block resized, keeping its bytes; `block` may be null. Null when the range is full. */
  [[nodiscard]] void* ArenaReallocate(void* block, std::size_t oldBytes, std::size_t bytes) noexcept;

  /** Frees a block of the range; `block` may be null. */
  void ArenaFree(void* block) noexcept;

  /**
   * A light userdata's pointer as a 32-bit value: a cell in the range holding the pointer. Light
   * userdata can name anything, an image's address or a module handle, so it does not fit 32 bits.
   * The same pointer always gets the same cell, so it compares and hashes as itself. Cells are never
   * freed: the engine pushes a handful of pointers.
   */
  [[nodiscard]] void* const* ArenaLightCell(void* pointer) noexcept;

#if defined(FAF_POSIX)
  // macOS keeps the low 4 GB unmapped for every process (__PAGEZERO), so the range lies above it:
  // a 4-byte field holds the offset from gArenaPointerBase, just below the range; 0 stays null.
  extern std::uintptr_t gArenaPointerBase;
#endif

  /** A Lua object's pointer as its 4-byte field holds it (LuaPrimitives.h), 0 for null. */
  [[nodiscard]] inline std::uint32_t ArenaPointerBits(const void* const pointer) noexcept
  {
#if defined(FAF_POSIX)
    return pointer != nullptr ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) - gArenaPointerBase) : 0u;
#else
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
#endif
  }

  /** The pointer a 4-byte field holds. */
  [[nodiscard]] inline void* ArenaPointerFromBits(const std::uint32_t bits) noexcept
  {
#if defined(FAF_POSIX)
    return bits != 0u ? reinterpret_cast<void*>(gArenaPointerBase + bits) : nullptr;
#else
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(bits));
#endif
  }
} // namespace lua
#endif
