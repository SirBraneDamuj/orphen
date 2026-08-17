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

#include "ported/sound/original_sequence_player.h"
#include "ported/sound/original_sound_bank.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
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

  // == Music ==
  //
  // FUN_00206840 keeps eight sequence slots. Slot 0 draws from music category 0,
  // slot 1 from category 1 and slots 2..7 all from category 2 -- the `min(i, 2)`
  // in FUN_0025b2f0 and FUN_00206840 alike. Each slot owns a whole SND.BIN
  // resource, loaded into banks 3..10 of the original's array; here each slot
  // owns its own SequencePlayer instead, which owns its own bank.
  inline constexpr std::size_t kMusicSlotCount = 8;
  inline constexpr std::size_t kMusicCategoryCount = 3;

  inline std::size_t musicCategoryForSlot(std::size_t slot)
  {
    return slot < kMusicCategoryCount ? slot : kMusicCategoryCount - 1;
  }

  // One eight-byte record of a category table. FUN_00228e28:89-117 copies these
  // out of SCR.BIN resource 199, from the three offsets at header word 7, and
  // stops at the first record whose resource id is zero.
  struct MusicRecord
  {
    std::uint16_t sndResource = 0; // +0
    std::uint8_t volume = 0;       // +2
    std::int16_t reverbType = -1;  // +4, -1 for "leave the reverb alone"
    std::uint16_t reverbDepth = 0; // +6; bit 0 is a flag, not part of the depth
  };

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
    // FUN_00205778's answer: when >= 0 the waveform comes from that music
    // slot's bank rather than from one of the three boot banks.
    std::int8_t musicSlot = -1;
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

    // --music-solo. Mutes the effect pool and the voice line in the mixer so a
    // --sound-dump contains only the sequence slots. Diagnostic only: dialogue
    // is centred and full-scale, so it buries the music in any measurement of
    // the whole mix. Nothing about the simulation changes.
    void setMusicSolo(bool solo) { musicSolo_ = solo; }

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

    // == The voice line ==
    //
    // FUN_00206d98 hands the clip to FUN_00207010, which programs a *reserved*
    // SPU2 voice and streams into it -- it never goes near FUN_002057c8's
    // 22-voice effect pool, so a long line cannot be stolen by a footstep. The
    // port keeps that separation: one dedicated slot, owning its own samples.
    //
    // Nothing here feeds back into the simulation. How long the line holds comes
    // from `VoiceIndex`, not from how much of the buffer the mixer has consumed,
    // so a run with no audio device keeps identical timing.
    void FUN_00207010_play_voice_line(std::vector<std::int16_t> pcm, float sampleRate);
    // FUN_00206a48, which drops DAT_00356788 back to zero.
    void FUN_00206a48_stop_voice_line();

    // == The music slots ==
    //
    // FUN_00228e28:89-117's three category tables, out of the same resource 199
    // the cue table comes from.
    bool loadMusicTables(std::span<const std::uint8_t> resource199);
    const MusicRecord *musicRecord(std::size_t category, std::size_t index) const;
    std::size_t musicRecordCount(std::size_t category) const;

    // FUN_00205938: put a bank resource into a slot. The caller resolves the
    // SND.BIN id through musicRecord() first and passes it back for the report.
    bool FUN_00205938_load_slot(std::size_t slot, std::uint16_t sndResource,
                                std::uint8_t baseVolume,
                                std::span<const std::uint8_t> bankResource);
    // FUN_00205d90 (opcode 0x129), FUN_002063c8 (0x12A) and FUN_00206260
    // (0x12B). The fader runs 0..1000 over the record's own volume byte.
    void FUN_00205d90_play_slot(std::size_t slot, int fader);
    void FUN_002063c8_ramp_up_slot(std::size_t slot, int speed, int targetFader);
    void FUN_00206260_ramp_down_slot(std::size_t slot, int speed, int targetFader);
    // == The scene-streamed sound effects ==
    //
    // A cue whose record byte +7 is non-zero does not name one of the three
    // boot banks. FUN_00205778 takes that byte and searches the *music slot
    // requests* -- DAT_00356a18, the shadow of the scene's own eight, indices 2
    // through 7 -- for the one whose index matches, and returns `slot + 3`,
    // which is that slot's bank. So these effects live in the same SND.BIN
    // resource as a piece of music: in s01_e012 cues 677..680 carry +7 = 127,
    // slot 6 requests category-2 index 127, and that is SND resource 213 --
    // the bank whose sequence is the cue under Sephy's scene and whose program
    // 4 holds Volcan's sword.
    //
    // Returns the slot, or -1 when the scene has not loaded a matching one --
    // where the original calls FUN_002683a8 and gives up.
    std::int8_t FUN_00205778_resolve_alternate_bank(std::uint8_t alternateId) const;
    // DAT_00356a18. FUN_00206840 copies the scene's requests here after acting
    // on them; the port records each slot's index as it loads it.
    void setSlotRequestIndex(std::size_t slot, std::uint16_t index);

    bool slotPlaying(std::size_t slot) const;
    bool slotHasSequence(std::size_t slot) const;
    const SequencePlayer &musicSlot(std::size_t slot) const { return musicSlots_[slot]; }

    // --sound-report's music section: what each slot ended up holding.
    struct MusicSlotLog
    {
      std::size_t slot = 0;
      std::size_t category = 0;
      std::uint16_t request = 0; // the raw u16 from the scene header
      std::uint16_t index = 0;
      std::uint16_t sndResource = 0;
      std::uint8_t volume = 0;
      bool autoPlayed = false;
      const char *outcome = "";
    };
    void logMusicSlot(const MusicSlotLog &entry) { musicLog_.push_back(entry); }
    const std::vector<MusicSlotLog> &musicLog() const { return musicLog_; }

    struct MusicEventLog
    {
      std::uint32_t frame = 0;
      std::size_t slot = 0;
      const char *action = "";
      std::uint16_t sndResource = 0;
      int speed = 0;
      int fader = 0;
    };
    const std::vector<MusicEventLog> &musicEventLog() const { return musicEventLog_; }

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
      // FUN_00267a80's two points, for the cues it drops as too far.
      float distance = -1.0f;
      float sourceX = 0.0f, sourceY = 0.0f, sourceZ = 0.0f;
      float listenerX = 0.0f, listenerY = 0.0f, listenerZ = 0.0f;
      const char *outcome = "";
    };
    const std::vector<CueLogEntry> &cueLog() const { return cueLog_; }
    void setFrame(std::uint32_t frame) { frame_ = frame; }

  private:
    struct Voice
    {
      const WaveformPcm *pcm = nullptr;
      double position = 0.0;
      double step = 1.0;
      float gainLeft = 0.0f;
      float gainRight = 0.0f;
      bool active = false;
    };

    void FUN_002057c8_key_on(std::uint16_t cue, int volumeLeft, int volumeRight);
    void startVoice(const KeyOn &request);
    const SoundBank *bankFor(const KeyOn &request) const;

    std::vector<SoundCue> cues_;
    std::array<SoundBank, kBankCount> banks_;
    Listener listener_;
    std::string diagnostic_;
    std::uint32_t frame_ = 0;
    float lastDistance_ = -1.0f;
    std::array<float, 3> lastSource_{};
    std::array<float, 3> lastListener_{};

    std::mutex mixLock_;
    std::vector<KeyOn> pending_;
    // 22 is the pool FUN_002057c8 polls before it starts a cue.
    std::array<Voice, 22> voices_{};
    // The reserved streaming voice. Owns its samples, unlike the pool, whose
    // waveforms belong to a bank that outlives them.
    std::vector<std::int16_t> voiceLinePcm_;
    double voiceLinePosition_ = 0.0;
    double voiceLineStep_ = 1.0;
    bool voiceLineActive_ = false;
    std::vector<CueLogEntry> cueLog_;

    // The eight sequence slots. Mutated by the simulation, rendered by the
    // mixer, so every touch of one is under mixLock_.
    std::array<SequencePlayer, kMusicSlotCount> musicSlots_;
    std::array<std::vector<MusicRecord>, kMusicCategoryCount> musicTables_;
    std::array<std::uint16_t, kMusicSlotCount> slotResource_{};
    // DAT_00356a18, the shadow of the scene's eight music requests.
    std::array<std::uint16_t, kMusicSlotCount> DAT_00356a18_slotRequestIndex_{};
    bool musicSolo_ = false;
    std::vector<MusicSlotLog> musicLog_;
    std::vector<MusicEventLog> musicEventLog_;
  };

} // namespace orphen::ported::sound
