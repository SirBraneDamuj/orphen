#pragma once

// The game's texture slot cache.
//
//   src/FUN_00221d20.c  the cache: scan a slot range for a hit, else take the
//                       first free slot and load into it
//   src/FUN_00210280.c  load one texture into one slot
//   src/FUN_00210348.c  "does this slot hold anything", a read of DAT_003429a8
//   src/FUN_00266118.c  ensure a model record's texture is bound, choosing the
//                       bank from the record's flags
//   src/FUN_00221fd8.c  boot: seven hardcoded binds, then every static record
//                       whose +0x06 is 'd' binds to its own slot
//
// Two parallel arrays, both indexed by slot:
//
//   DAT_00315a98  s16, the cache's residency key. Set only by FUN_00221d20, so
//                 statically bound slots leave it at zero. Stored negated when
//                 the model record's flags carry 0x40.
//   DAT_003429a8  u32, the texture id actually resident. Set by FUN_00210280,
//                 so it covers both the cached and the static paths.
//
// Reading both out of an EE dump of s01_e024 is what pins this down: the cache
// half holds 0206 01bf 01bc 0204 0203 0200 0202 0201 in slots 10..17 and 0195
// in slot 24, while the static half additionally holds the boot textures in
// slots 32..37, 40, 42..44 and 48.
//
// What the port does *not* port is FUN_00210368 / FUN_002103d0, the GS BITBLT
// packet that moves the decoded pixels into VRAM. A slot here owns a decoded
// RGBA image instead; whoever renders turns that into a texture object.

#include "ported/resource/bmpa_texture_decoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::resource
{

  // DAT_00315a98 and DAT_003429a8 are both larger than this, but 0x30 is the
  // highest slot any code path reaches (FUN_00229c40 clamps the entity's copy
  // of the slot index to 0x30) so 64 covers everything with room to spare.
  inline constexpr std::size_t kTextureSlotCount = 64;

  // FUN_00266118 lines 13-21. The banks do not overlap and neither reaches the
  // static slots at 32 and above.
  inline constexpr int kDefaultBankStart = 10;
  inline constexpr int kDefaultBankCount = 14; // slots 10..23
  inline constexpr int kAltBankStart = 24;
  inline constexpr int kAltBankCount = 16; // slots 24..39

  // Returned by acquire when every slot in the range is taken. The original
  // reports "ER_TEXTURE" through FUN_0026bfc0 at 0x34BA08 and returns slot 0.
  inline constexpr int kNoTextureSlot = -1;

  struct TextureSlotState
  {
    std::int16_t DAT_00315a98_cacheKey = 0;
    std::uint32_t DAT_003429a8_residentId = 0;
    BmpaTexture texture;

    // FUN_00210348: a slot counts as occupied once something has been loaded
    // into it, whether through the cache or statically.
    bool occupied() const { return DAT_003429a8_residentId != 0; }
  };

  class TextureSlotCache
  {
  public:
    // Resolves a texture resource id to decoded pixels. Returns nullopt when the
    // resource is not in any bundle the port has open, which is not fatal: the
    // slot stays empty and the caller reports it.
    using TextureLoader = std::function<std::optional<BmpaTexture>(std::uint16_t textureId)>;

    void setLoader(TextureLoader loader) { loader_ = std::move(loader); }
    void reset();

    // FUN_00210280(slot, 3, textureId). Loads unconditionally; the callers do
    // the "is it already there" test themselves.
    bool FUN_00210280_load_into_slot(int slot, std::uint16_t textureId);

    // FUN_00221d20(record, rangeStart, rangeCount). `negateCacheKey` is the
    // record's flags bit 0x40.
    int FUN_00221d20_acquire_slot(std::uint16_t textureId,
                                  bool negateCacheKey,
                                  int rangeStart,
                                  int rangeCount);

    // FUN_00266118's bank choice wrapped around the acquire above.
    int FUN_00266118_bind_texture(std::uint16_t textureId, bool useAltBank, bool negateCacheKey);

    const TextureSlotState &slot(std::size_t index) const;
    std::size_t occupiedSlots() const;

    // Slots the loader could not fill. Counted rather than thrown, so a bundle
    // missing one texture does not take the whole scene down.
    std::size_t missingTextures() const { return missingTextures_; }

    // Bumped by reset and by every load. Consumers that mirror these pixels
    // into their own objects -- a GL texture per slot, say -- have to key their
    // cache on this rather than on the address of the cache, which never
    // changes: the slots are refilled in place on a scene reload, so pointer
    // identity says "unchanged" at exactly the moment everything changed.
    std::uint64_t generation() const { return generation_; }

  private:
    std::array<TextureSlotState, kTextureSlotCount> slots_{};
    TextureLoader loader_;
    std::size_t missingTextures_ = 0;
    std::uint64_t generation_ = 0;
  };

} // namespace orphen::ported::resource
