#pragma once

// The sound-effect path, from a cue number to a pair of stereo gains and a
// waveform.
//
//   src/FUN_00267d38.c  the entry point behaviours call: with an entity, play
//                       positionally; without one, play flat
//   src/FUN_00267a80.c  distance attenuation and panning against the camera
//   src/FUN_002057c8.c  the cue table lookup and the volume scaling, then the
//                       0x4069 command to the IOP
//   src/FUN_00228e28.c  loads the cue table: SCR.BIN resource 199
//   src/FUN_00205118.c  loads the banks: SND.BIN resources 1, 2 and 3
//
// == What a cue is ==
//
// DAT_00354BF0 points at 711 eight-byte records, copied out of SCR.BIN
// resource 199 at boot. Only five of the eight bytes matter here:
//
//   +0  bank      logical bank, when +7 is zero
//   +1  program   VAB program within that bank
//   +2  note      MIDI note; in bank 0 this selects the waveform, not a pitch
//   +3  volume    scale, 0..127
//   +5  cap       ceiling on the scaled volume, 0 for none
//   +7  alternate when non-zero, FUN_00205778 picks the bank instead of +0
//
// The alternate-bank path is the scene-streamed banks and is not ported; a cue
// that needs one is reported rather than played.
//
// == What the IOP is handed ==
//
//   FUN_00204d88(0x4069, (vabId << 8) | program, note << 8, volLeft, volRight)
//
// so the port's job stops at (bank, program, note, volLeft, volRight) and the
// mixer takes it from there. The IOP applies the tone's own volume and the
// program's on top, which is why SoundEngine multiplies both back in.
//
// == Threading ==
//
// The simulation only ever appends to a queue. Mixing happens wherever the
// audio callback runs, under `mixLock`. Nothing the mixer does can reach the
// simulation, so `--frames` stays byte-identical whether or not a device is
// open -- and headless runs never open one.

#include "ported/sound/original_sound_bank.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::sound
{

  // FUN_00205118: three banks at boot, from SND.BIN resources 1, 2 and 3.
  inline constexpr std::size_t kBankCount = 3;
  inline constexpr std::array<std::uint16_t, kBankCount> kBootBankResources{1, 2, 3};

  // FUN_002057c8's record.
  struct SoundCue
  {
    std::uint8_t bank = 0;
    std::uint8_t program = 0;
    std::uint8_t note = 60;
    std::uint8_t volume = 0;
    std::uint8_t cap = 0;
    std::uint8_t alternateBank = 0;
    bool usable() const { return volume != 0 || note != 0; }
  };

  // FUN_00267a80's constants, at 0x00352D0C and 0x00352D10.
  inline constexpr float fGpffff8d9c_flatDistance = 0.3f;
  inline constexpr float fGpffff8da0_panDeadZone = 0.1f;
  // Inline in FUN_00267a80: nothing is audible past 14 units, the near field
  // that keeps a sound centred is 3, and the pan swings +/-40 about 110.
  inline constexpr float kAudibleRange = 14.0f;
  inline constexpr float kNearField = 3.0f;
  inline constexpr int kPanSwing = 40;
  inline constexpr int kPanCentre = 0x6E;
  // DAT_003555d5, the master scale. The original's options menu moves it; the
  // port holds it at the maximum.
  inline constexpr int DAT_003555d5_masterVolume = 128;

  struct Listener
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yawRadians = 0.0f; // uGpffffb6d4
  };

  // One cue that was asked for this frame, after FUN_002057c8's scaling.
  struct KeyOn
  {
    std::uint16_t cue = 0;
    std::uint8_t bank = 0;
    std::uint8_t program = 0;
    std::uint8_t note = 60;
    std::uint8_t volumeLeft = 0;
    std::uint8_t volumeRight = 0;
  };

  class SoundEngine
  {
  public:
    // SCR.BIN resource 199, already decompressed.
    bool FUN_00228e28_load_cue_table(std::span<const std::uint8_t> resource199);
    // One SND.BIN resource, raw.
    bool FUN_00205310_load_bank(std::size_t bank, std::span<const std::uint8_t> resource);

    bool cueTableLoaded() const { return !cues_.empty(); }
    std::size_t cueCount() const { return cues_.size(); }
    bool bankLoaded(std::size_t bank) const { return bank < kBankCount && banks_[bank].valid(); }
    const SoundBank &bank(std::size_t index) const { return banks_[index]; }
    const std::string &diagnostic() const { return diagnostic_; }

    void setListener(const Listener &listener) { listener_ = listener; }

    // FUN_00267d38 with a non-zero entity: FUN_00267a80(x, y, z, cue, 100).
    void FUN_00267d38_play_at(std::uint16_t cue, float x, float y, float z);
    // FUN_00267d38 with a null entity, which reaches FUN_002057c8 at full pan.
    void FUN_00267d38_play_flat(std::uint16_t cue);

    // Takes the queue over to the mixer's own voices. mix() does this itself;
    // a headless run calls it directly so the queue cannot grow without bound.
    void drainPendingKeyOns();
    // Renders interleaved stereo at kSpuBaseSampleRate, overwriting the buffer.
    // Runs on whichever thread the audio callback uses.
    void mix(float *interleavedStereo, std::size_t frames);

    // --sound-report. Cue numbers in the order they were asked for, with what
    // happened to each.
    struct CueLogEntry
    {
      std::uint16_t cue = 0;
      std::uint32_t frame = 0;
      std::uint8_t bank = 0;
      std::uint8_t program = 0;
      std::uint8_t note = 0;
      std::uint8_t volumeLeft = 0;
      std::uint8_t volumeRight = 0;
      std::int16_t waveform = 0;
      std::size_t samples = 0;
      float sampleRate = 0.0f;
      const char *outcome = "";
    };
    const std::vector<CueLogEntry> &cueLog() const { return cueLog_; }
    void setFrame(std::uint32_t frame) { frame_ = frame; }

  private:
    struct Voice
    {
      const std::vector<std::int16_t> *pcm = nullptr;
      double position = 0.0;
      double step = 1.0;
      float gainLeft = 0.0f;
      float gainRight = 0.0f;
      bool active = false;
    };

    void FUN_002057c8_key_on(std::uint16_t cue, int volumeLeft, int volumeRight);
    void startVoice(const KeyOn &request);

    std::vector<SoundCue> cues_;
    std::array<SoundBank, kBankCount> banks_;
    Listener listener_;
    std::string diagnostic_;
    std::uint32_t frame_ = 0;

    std::mutex mixLock_;
    std::vector<KeyOn> pending_;
    // 22 is the pool FUN_002057c8 polls before it starts a cue.
    std::array<Voice, 22> voices_{};
    std::vector<CueLogEntry> cueLog_;
  };

} // namespace orphen::ported::sound
