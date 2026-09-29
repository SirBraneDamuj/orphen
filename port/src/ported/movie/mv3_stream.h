#pragma once

// The MV3 movie container: one header sector, then fixed-size blocks that each
// carry a slice of PCM and a slice of MPEG-2 video.
//
//   src/FUN_002f1a70.c  open the file, read sector 0, validate the header
//   src/FUN_002f1c98.c  read one block and copy its two slices into the rings
//
// The header is six little-endian words -- the field names are the debug
// string at 0x0034FE30, "blk_byte=%d,pcm_ofs=%d,pcm_1sz=%d,mpg_ofs=%d,mpg_1sz=%d":
//
//   +0x00  'MV30'
//   +0x04  blk_byte   size of one block
//   +0x08  pcm_ofs    where the PCM slice sits inside a block
//   +0x0C  pcm_1sz    how much of it there is
//   +0x10  mpg_ofs    likewise for the MPEG slice
//   +0x14  mpg_1sz
//
// The rest of sector 0 is ignored and the first block starts at 0x800. Every
// Mxx.MV3 on the US disc has the same layout: 0xAA800-byte blocks holding
// 0x30000 bytes of PCM at +0 and 0x7A120 of MPEG at +0x30000.
//
// **The PCM is not LRLR.** It is signed 16-bit, 48 kHz stereo, in alternating
// 0x200-byte runs of one channel -- the block shape SPU2's AutoDMA input takes,
// which is why FUN_002F2110 can hand the ring to the IOP untouched. See
// docs/mv3_movie_playback_analysis.md for how the stripe width was pinned.
//
// Not ported: the rings themselves (0x01949A00 and 0x007C9A00) and the CD
// streaming state machine behind FUN_002F1C98. The port reads a block when it
// wants one, which is all the rings existed to make possible.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace orphen::ported::movie
{

  // 'MV30' as FUN_002F1A70 compares it: one little-endian word.
  inline constexpr std::uint32_t kMv3Magic = 0x3033564D;
  inline constexpr std::uint32_t kMv3SectorBytes = 0x800;
  // FUN_002F1A70's two bounds checks, against the fixed ring sizes. Both fail
  // with "[%s] mpg_block size over.".
  inline constexpr std::uint32_t kMpegRingBytes = 0x600000;
  inline constexpr std::uint32_t kMpegRingChunks = 5;
  inline constexpr std::uint32_t kPcmRingBytes = 0x200000;
  inline constexpr std::uint32_t kPcmRingChunks = 8;

  // One channel's run inside the PCM slice.
  inline constexpr std::uint32_t kPcmStripeBytes = 0x200;
  inline constexpr int kPcmSampleRate = 48000;

  struct Mv3Header
  {
    std::uint32_t blk_byte = 0;
    std::uint32_t pcm_ofs = 0;
    std::uint32_t pcm_1sz = 0;
    std::uint32_t mpg_ofs = 0;
    std::uint32_t mpg_1sz = 0;
  };

  class Mv3Stream
  {
  public:
    // FUN_002F1A70. False, with diagnostic() saying why, for a missing file,
    // a bad magic or a slice the original's rings could not hold.
    bool open(const std::filesystem::path &path);
    bool isOpen() const { return file_.is_open(); }
    const Mv3Header &header() const { return header_; }
    const std::string &diagnostic() const { return diagnostic_; }

    // FUN_002F1C98 for one whole block. Appends the block's MPEG slice to
    // `mpeg` and its PCM slice, already de-striped into interleaved stereo, to
    // `pcm`. False once no complete block is left -- the original only ever
    // copies out of a finished block.
    bool readBlock(std::vector<std::uint8_t> &mpeg, std::vector<std::int16_t> &pcm);

    std::uint32_t blocksRead() const { return blocksRead_; }
    std::uint32_t blockCount() const { return blockCount_; }

  private:
    std::ifstream file_;
    Mv3Header header_;
    std::string diagnostic_;
    std::vector<std::uint8_t> block_;
    std::uint32_t blocksRead_ = 0;
    std::uint32_t blockCount_ = 0;
  };

  // The 0x200-byte channel runs into ordinary interleaved stereo. A trailing
  // run with no partner is dropped; every real slice is a whole number of
  // pairs.
  void unstripePcm(const std::uint8_t *slice, std::size_t bytes, std::vector<std::int16_t> &out);

} // namespace orphen::ported::movie
