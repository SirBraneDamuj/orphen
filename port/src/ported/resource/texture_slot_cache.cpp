#include "ported/resource/texture_slot_cache.h"

#include <algorithm>

namespace orphen::ported::resource
{

  void TextureSlotCache::reset()
  {
    slots_ = {};
    missingTextures_ = 0;
  }

  bool TextureSlotCache::FUN_00210280_load_into_slot(int slot, std::uint16_t textureId)
  {
    if (slot < 0 || static_cast<std::size_t>(slot) >= kTextureSlotCount)
    {
      return false;
    }

    // FUN_00210280 writes the residency word first, then reads and uploads.
    // Keeping that order matters: the port marks the slot taken even when the
    // resource is missing, so a second request does not keep retrying it.
    TextureSlotState &state = slots_[static_cast<std::size_t>(slot)];
    state.DAT_003429a8_residentId = textureId;

    std::optional<BmpaTexture> decoded;
    if (loader_)
    {
      decoded = loader_(textureId);
    }
    if (!decoded.has_value())
    {
      ++missingTextures_;
      state.texture = BmpaTexture{};
      return false;
    }

    state.texture = std::move(*decoded);
    return true;
  }

  int TextureSlotCache::FUN_00221d20_acquire_slot(std::uint16_t textureId,
                                                  bool negateCacheKey,
                                                  int rangeStart,
                                                  int rangeCount)
  {
    const int rangeEnd = rangeStart + rangeCount;
    // The key the scan compares against. FUN_00221d20 sign-extends the negation
    // rather than negating the sign-extended value, which is the same thing for
    // every id the game actually uses.
    const std::int16_t wantedKey =
        negateCacheKey ? static_cast<std::int16_t>(-static_cast<std::int16_t>(textureId))
                       : static_cast<std::int16_t>(textureId);

    int firstFree = kNoTextureSlot;
    for (int slot = rangeStart; slot < rangeEnd; ++slot)
    {
      if (slot < 0 || static_cast<std::size_t>(slot) >= kTextureSlotCount)
      {
        continue;
      }
      const TextureSlotState &state = slots_[static_cast<std::size_t>(slot)];

      if (state.DAT_00315a98_cacheKey == 0)
      {
        // A slot is only free if nothing has been loaded into it either. That
        // second test is what keeps the cache off the statically bound slots.
        if (firstFree == kNoTextureSlot && !state.occupied())
        {
          firstFree = slot;
        }
        continue;
      }

      if (state.DAT_00315a98_cacheKey == wantedKey && state.occupied())
      {
        return slot;
      }
    }

    if (firstFree == kNoTextureSlot)
    {
      return kNoTextureSlot;
    }

    FUN_00210280_load_into_slot(firstFree, textureId);
    slots_[static_cast<std::size_t>(firstFree)].DAT_00315a98_cacheKey = wantedKey;
    return firstFree;
  }

  int TextureSlotCache::FUN_00266118_bind_texture(std::uint16_t textureId,
                                                  bool useAltBank,
                                                  bool negateCacheKey)
  {
    if (textureId == 0)
    {
      return kNoTextureSlot;
    }
    return useAltBank
               ? FUN_00221d20_acquire_slot(textureId, negateCacheKey, kAltBankStart, kAltBankCount)
               : FUN_00221d20_acquire_slot(textureId, negateCacheKey, kDefaultBankStart,
                                           kDefaultBankCount);
  }

  const TextureSlotState &TextureSlotCache::slot(std::size_t index) const
  {
    static const TextureSlotState empty{};
    return index < kTextureSlotCount ? slots_[index] : empty;
  }

  std::size_t TextureSlotCache::occupiedSlots() const
  {
    return static_cast<std::size_t>(
        std::count_if(slots_.begin(), slots_.end(),
                      [](const TextureSlotState &state) { return state.occupied(); }));
  }

} // namespace orphen::ported::resource
