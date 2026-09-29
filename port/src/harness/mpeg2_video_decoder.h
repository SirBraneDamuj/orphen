#pragma once

// MPEG-2 video for the MV3 movies, through FFmpeg's libavcodec.
//
// No original counterpart. The PS2 decoded these on the IPU, driven by Sony's
// libmpeg ("PsIIlibmpeg 1510" in the ELF): FUN_002FC9E8 is sceMpegGetPicture
// and FUN_002FCB00 sceMpegIsEnd. Nothing of that is ported; this is the
// substitute, in the same way AudioDevice stands in for SPU2.
//
// The streams are MPEG-2 Main Profile, 640x480, 29.97 fps, interlaced frame
// pictures with B-frames -- full MP@ML, which is why this is libavcodec and not
// a small MPEG-1 decoder.
//
// What *is* the original's is the colour conversion. The IPU's CSC turns a
// macroblock into RGB itself, and ipuCscToRgba below does it the same way:
// BT.601 studio range in fixed point, chroma repeated over each 2x2 block
// rather than interpolated. The coefficients are PCSX2's reference model of
// the IPU (0x95, 0xCC, -0x68, -0x32, 0x102 over a Y bias of 16). They have not
// been checked against a capture from hardware.
//
// Builds without FFmpeg compile the same interface and open() returns false,
// so the caller falls back to logging the movie.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace orphen::harness
{

  // One decoded picture, planar 4:2:0.
  struct YuvPicture
  {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> y;  // width * height
    std::vector<std::uint8_t> cb; // (width / 2) * (height / 2)
    std::vector<std::uint8_t> cr;
  };

  class Mpeg2VideoDecoder
  {
  public:
    Mpeg2VideoDecoder();
    ~Mpeg2VideoDecoder();
    Mpeg2VideoDecoder(const Mpeg2VideoDecoder &) = delete;
    Mpeg2VideoDecoder &operator=(const Mpeg2VideoDecoder &) = delete;

    // False when the port was built without FFmpeg, or the codec will not open.
    bool open();
    static bool available();

    // Elementary-stream bytes, in order. endOfStream() says no more will come,
    // so the decoder can give up the pictures it is holding back.
    void feed(std::span<const std::uint8_t> bytes);
    void endOfStream();

    // The next picture in display order. False means either "feed me more" or,
    // once finished() is true, that the stream is done.
    bool nextPicture(YuvPicture &out);
    bool finished() const;

    const std::string &diagnostic() const { return diagnostic_; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string diagnostic_;
  };

  // The IPU's CSC over a whole picture, keeping rows [firstRow, firstRow +
  // rows). Output is RGBA8, alpha 0xFF.
  void ipuCscToRgba(const YuvPicture &picture, int firstRow, int rows, std::vector<std::uint8_t> &rgba);

} // namespace orphen::harness
