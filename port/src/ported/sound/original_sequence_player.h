#pragma once

// The music path: a Sony SEQ stream driving a VAB, which is what the game's
// background music and its ambient beds actually are.
//
//   src/FUN_00205938.c  load a slot: open the bank, hand section 2 to the
//                       sequencer, set reverb, optionally start it
//   src/FUN_00205d90.c  play a loaded slot (opcode 0x129 reaches this)
//   src/FUN_002063c8.c  fade a slot out (0x12A)
//   src/FUN_00206260.c  stop a slot (0x12B)
//   src/FUN_00206840.c  the scene's own eight requests, at load
//   src/FUN_0025b2f0.c  where those requests come from: scene header word 10
//
// == Why this exists ==
//
// A SND.BIN bank resource has three sections; the port already read two of
// them. Section 2 is either the four bytes "NSEQ" -- FUN_00205548's marker for
// "there is no sequence here" -- or a real SEQp chunk. The three banks loaded
// at boot are all NSEQ, which is exactly why the port could play every sound
// effect and no music at all: every note of every piece lives in a section the
// loader was throwing away.
//
// == The format ==
//
// Standard Sony SEQp: a 15-byte big-endian header, then one MIDI track with
// variable-length delta times and running status.
//
//   +0x00  'SEQp'                (reads "pQES" byte by byte, little-endian)
//   +0x04  u32  version
//   +0x08  u16  ppqn, ticks per quarter note
//   +0x0A  u24  tempo, microseconds per quarter note
//   +0x0D  u16  rhythm (time signature); unused here
//   +0x0F  the event stream
//
// Events are the MIDI subset: note off/on, control change, program change,
// pitch bend, and the 0xFF 0x2F end-of-track. Loops are Sony's CC convention
// rather than anything in the header:
//
//   CC99 = 20   loop start
//   CC99 = 30   loop end
//   CC6  = n    loop count, 127 meaning forever
//
// The ambient bed in s01_e012 (SND resource 112) is exactly one held note
// between such a pair, which is why it plays for the whole scene.

#include "ported/sound/original_sound_bank.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::sound
{

  // 16 MIDI channels; the pieces in this game use at most a handful.
  inline constexpr std::size_t kSequenceChannels = 16;
  // SPU2 has 48 voices and the effect pool takes 22. The rest is more than any
  // of these sequences asks for at once.
  inline constexpr std::size_t kSequenceVoices = 24;

  struct SequenceChannel
  {
    std::uint8_t program = 0;
    std::uint8_t volume = 127;     // CC7
    std::uint8_t pan = 64;         // CC10
    std::uint8_t expression = 127; // CC11
    bool sustainPedal = false;     // CC64
    float bendCents = 0.0f;
  };

  struct SequenceVoice
  {
    const WaveformPcm *pcm = nullptr;
    const VabTone *tone = nullptr;
    AdsrEnvelope envelope;
    double position = 0.0;
    double step = 1.0;
    float gainLeft = 0.0f;
    float gainRight = 0.0f;
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 127;
    bool active = false;
    bool held = false; // key is down; a release only starts once this clears
  };

  // One of the eight slots FUN_00205938 manages. Owns its bank, because each
  // slot is a whole separate SND.BIN resource.
  class SequencePlayer
  {
  public:
    // == The volume model ==
    //
    // FUN_00206048 keeps two numbers per slot: the record's own volume byte at
    // +0xB6/+0xB7 (0..127, the same value in both channels) and a *fader* at
    // +0xB4 running 0..1000. What reaches the sequencer is
    // `fader * base / 1000`, so a fader of 1000 means "this slot's authored
    // volume" -- which is exactly the 1000 FUN_00206840 and opcode 0x129 both
    // pass.
    static constexpr int kFaderFull = 1000;

    // The bank resource, whole, plus the record's volume byte. Returns false
    // when it has no SEQp section, which is the normal case for a sound-effect
    // bank.
    bool FUN_00205938_load(std::span<const std::uint8_t> bankResource, std::uint8_t baseVolume);
    bool loaded() const { return loaded_; }
    bool hasSequence() const { return !events_.empty(); }

    // FUN_00205d90. `fader` is the 0..1000 scale above.
    void FUN_00205d90_play(int fader);
    // Hard stop, silencing every voice. FUN_00206260 only reaches this when its
    // ramp has nowhere to go.
    void stop();
    // FUN_002063c8 (rampUp) and FUN_00206260 (rampDown): move the fader toward
    // `targetFader` at a rate `speed` selects, and on a downward ramp stop the
    // slot when it lands. The frame count is the original's:
    //
    //   step  = |current - target| * base / 1000   (the 0..127 delta)
    //   scale = speed >= 8 ? 1 : speed >= 4 ? 2 : speed >= 2 ? 3 : 4
    //   frames = step * scale, halved when speed > 13, then + 10
    void ramp(int targetFader, int speed, bool rampUp);

    bool playing() const { return playing_; }
    // Reporting: how the track has actually run. A piece that stops when it
    // should repeat shows up here as loopsTaken 0 with reachedEndOfTrack set.
    std::uint32_t loopsTaken() const { return loopsTaken_; }
    bool reachedEndOfTrack() const { return reachedEndOfTrack_; }
    // True when the event stream stopped making sense. Should never happen on
    // retail data; if it does, the parser and the format have diverged.
    bool desynced() const { return desynced_; }
    int fader() const { return static_cast<int>(fader_); }
    // Still making sound: either running, or holding voices through a release
    // after the last event. Rendering only while `playing_` would cut the tail
    // off every piece.
    bool audible() const;
    std::uint16_t ppqn() const { return ppqn_; }
    std::uint32_t microsecondsPerQuarter() const { return usPerQuarter_; }
    const SoundBank &bank() const { return bank_; }

    // Renders `frames` stereo frames additively at kSpuBaseSampleRate, running
    // the sequence forward as it goes. Called from the mixer thread only.
    void render(float *interleavedStereo, std::size_t frames);

  private:
    void reset();
    void runEvents();
    void noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity);
    void noteOff(std::uint8_t channel, std::uint8_t note);
    void allNotesOff();
    void updateVoiceGain(SequenceVoice &voice);
    std::uint32_t readVariableLength();

    SoundBank bank_;
    std::vector<std::uint8_t> events_;
    bool loaded_ = false;
    bool playing_ = false;

    std::uint16_t ppqn_ = 48;
    std::uint32_t usPerQuarter_ = 500000;
    std::size_t cursor_ = 0;
    std::uint8_t runningStatus_ = 0;
    // Output samples still owed to the current delta.
    double samplesUntilNextEvent_ = 0.0;
    // The stream is delta-then-event. This says the delta in front of the event
    // at `cursor_` has already been added to the wait, so a resumed render does
    // not read it twice.
    bool deltaConsumed_ = false;

    std::size_t loopCursor_ = 0;
    std::uint8_t loopRunningStatus_ = 0;
    bool haveLoopPoint_ = false;
    int loopsRemaining_ = 0;
    bool loopForever_ = false;
    std::uint32_t loopsTaken_ = 0;
    bool reachedEndOfTrack_ = false;
    bool desynced_ = false;

    std::uint8_t baseVolume_ = 127; // +0xB6 / +0xB7
    double fader_ = kFaderFull;     // +0xB4
    double targetFader_ = kFaderFull;
    double faderStep_ = 0.0; // per output sample
    bool ramping_ = false;
    bool stopWhenRampEnds_ = false;
    float masterGain() const;

    std::array<SequenceChannel, kSequenceChannels> channels_{};
    std::array<SequenceVoice, kSequenceVoices> voices_{};
  };

} // namespace orphen::ported::sound
