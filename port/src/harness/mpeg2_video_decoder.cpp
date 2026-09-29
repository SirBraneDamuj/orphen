#include "harness/mpeg2_video_decoder.h"

#include <algorithm>
#include <cstring>

#if ORPHEN_PORT_HAVE_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}
#endif

namespace orphen::harness
{

#if ORPHEN_PORT_HAVE_FFMPEG

  struct Mpeg2VideoDecoder::Impl
  {
    AVCodecContext *context = nullptr;
    AVCodecParserContext *parser = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;

    // Bytes fed but not yet parsed. av_parser_parse2 may read past the end of
    // what it is given, so the vector always carries
    // AV_INPUT_BUFFER_PADDING_SIZE zeros past `size`.
    std::vector<std::uint8_t> input;
    std::size_t size = 0;
    std::size_t readAt = 0;
    bool endOfStream = false;
    bool flushed = false;
    bool finished = false;

    ~Impl()
    {
      av_frame_free(&frame);
      av_packet_free(&packet);
      if (parser != nullptr)
      {
        av_parser_close(parser);
      }
      avcodec_free_context(&context);
    }

    void send(std::uint8_t *data, int bytes)
    {
      packet->data = data;
      packet->size = bytes;
      avcodec_send_packet(context, packet);
    }
  };

  Mpeg2VideoDecoder::Mpeg2VideoDecoder() = default;
  Mpeg2VideoDecoder::~Mpeg2VideoDecoder() = default;

  bool Mpeg2VideoDecoder::available() { return true; }

  bool Mpeg2VideoDecoder::open()
  {
    impl_ = std::make_unique<Impl>();
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    if (codec == nullptr)
    {
      diagnostic_ = "libavcodec has no MPEG-2 decoder";
      impl_.reset();
      return false;
    }
    impl_->context = avcodec_alloc_context3(codec);
    impl_->parser = av_parser_init(codec->id);
    impl_->packet = av_packet_alloc();
    impl_->frame = av_frame_alloc();
    if (impl_->context == nullptr || impl_->parser == nullptr || impl_->packet == nullptr ||
        impl_->frame == nullptr)
    {
      diagnostic_ = "out of memory opening the MPEG-2 decoder";
      impl_.reset();
      return false;
    }
    // One thread: a 640x480 picture is well under a millisecond, and a
    // threaded decoder holds pictures back, which only delays the first one.
    impl_->context->thread_count = 1;
    if (avcodec_open2(impl_->context, codec, nullptr) < 0)
    {
      diagnostic_ = "avcodec_open2 failed";
      impl_.reset();
      return false;
    }
    impl_->input.assign(AV_INPUT_BUFFER_PADDING_SIZE, 0);
    return true;
  }

  void Mpeg2VideoDecoder::feed(std::span<const std::uint8_t> bytes)
  {
    if (!impl_ || bytes.empty())
    {
      return;
    }
    Impl &impl = *impl_;
    // Drop what the parser has already consumed before growing the buffer.
    if (impl.readAt > 0)
    {
      std::memmove(impl.input.data(), impl.input.data() + impl.readAt, impl.size - impl.readAt);
      impl.size -= impl.readAt;
      impl.readAt = 0;
    }
    impl.input.resize(impl.size + bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE);
    std::memcpy(impl.input.data() + impl.size, bytes.data(), bytes.size());
    impl.size += bytes.size();
    std::memset(impl.input.data() + impl.size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
  }

  void Mpeg2VideoDecoder::endOfStream()
  {
    if (impl_)
    {
      impl_->endOfStream = true;
    }
  }

  bool Mpeg2VideoDecoder::finished() const { return !impl_ || impl_->finished; }

  bool Mpeg2VideoDecoder::nextPicture(YuvPicture &out)
  {
    if (!impl_ || impl_->finished)
    {
      return false;
    }
    Impl &impl = *impl_;
    for (;;)
    {
      const int received = avcodec_receive_frame(impl.context, impl.frame);
      if (received == 0)
      {
        const AVFrame &frame = *impl.frame;
        out.width = frame.width;
        out.height = frame.height;
        const int chromaWidth = frame.width / 2;
        const int chromaHeight = frame.height / 2;
        out.y.resize(static_cast<std::size_t>(frame.width) * frame.height);
        out.cb.resize(static_cast<std::size_t>(chromaWidth) * chromaHeight);
        out.cr.resize(out.cb.size());
        for (int row = 0; row < frame.height; ++row)
        {
          std::memcpy(out.y.data() + static_cast<std::size_t>(row) * frame.width,
                      frame.data[0] + static_cast<std::ptrdiff_t>(row) * frame.linesize[0],
                      static_cast<std::size_t>(frame.width));
        }
        for (int row = 0; row < chromaHeight; ++row)
        {
          std::memcpy(out.cb.data() + static_cast<std::size_t>(row) * chromaWidth,
                      frame.data[1] + static_cast<std::ptrdiff_t>(row) * frame.linesize[1],
                      static_cast<std::size_t>(chromaWidth));
          std::memcpy(out.cr.data() + static_cast<std::size_t>(row) * chromaWidth,
                      frame.data[2] + static_cast<std::ptrdiff_t>(row) * frame.linesize[2],
                      static_cast<std::size_t>(chromaWidth));
        }
        av_frame_unref(impl.frame);
        return true;
      }
      if (received != AVERROR(EAGAIN))
      {
        // AVERROR_EOF after the flush, or a hard error: either way, done.
        impl.finished = true;
        return false;
      }

      if (impl.readAt < impl.size)
      {
        std::uint8_t *packetData = nullptr;
        int packetBytes = 0;
        const int chunk = static_cast<int>(std::min<std::size_t>(impl.size - impl.readAt, 0x10000));
        const int used = av_parser_parse2(impl.parser, impl.context, &packetData, &packetBytes,
                                          impl.input.data() + impl.readAt, chunk,
                                          AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (used < 0)
        {
          diagnostic_ = "MPEG-2 parser error";
          impl.finished = true;
          return false;
        }
        impl.readAt += static_cast<std::size_t>(used);
        if (packetBytes > 0)
        {
          impl.send(packetData, packetBytes);
        }
        continue;
      }

      if (!impl.endOfStream)
      {
        return false;
      }
      if (!impl.flushed)
      {
        // An empty parse hands back whatever picture the parser was still
        // assembling; the null packet then drains the decoder's reorder queue.
        std::uint8_t *packetData = nullptr;
        int packetBytes = 0;
        av_parser_parse2(impl.parser, impl.context, &packetData, &packetBytes, nullptr, 0,
                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (packetBytes > 0)
        {
          impl.send(packetData, packetBytes);
        }
        avcodec_send_packet(impl.context, nullptr);
        impl.flushed = true;
        continue;
      }
      impl.finished = true;
      return false;
    }
  }

#else

  struct Mpeg2VideoDecoder::Impl
  {
  };

  Mpeg2VideoDecoder::Mpeg2VideoDecoder() = default;
  Mpeg2VideoDecoder::~Mpeg2VideoDecoder() = default;
  bool Mpeg2VideoDecoder::available() { return false; }
  bool Mpeg2VideoDecoder::open()
  {
    diagnostic_ = "built without FFmpeg";
    return false;
  }
  void Mpeg2VideoDecoder::feed(std::span<const std::uint8_t>) {}
  void Mpeg2VideoDecoder::endOfStream() {}
  bool Mpeg2VideoDecoder::nextPicture(YuvPicture &) { return false; }
  bool Mpeg2VideoDecoder::finished() const { return true; }

#endif

  void ipuCscToRgba(const YuvPicture &picture, int firstRow, int rows, std::vector<std::uint8_t> &rgba)
  {
    // 1/64 fixed point, with a final halving that rounds: effectively 1/128.
    constexpr int kYBias = 16;
    constexpr int kYCoefficient = 0x95;   // 1.164
    constexpr int kRCrCoefficient = 0xCC; // 1.594
    constexpr int kGCrCoefficient = -0x68;
    constexpr int kGCbCoefficient = -0x32;
    constexpr int kBCbCoefficient = 0x102; // 2.016

    const int width = picture.width;
    const int chromaWidth = width / 2;
    rgba.resize(static_cast<std::size_t>(width) * rows * 4);
    const auto clampByte = [](int value) {
      return static_cast<std::uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
    };

    for (int row = 0; row < rows; ++row)
    {
      const int sourceRow = std::clamp(firstRow + row, 0, picture.height - 1);
      const std::uint8_t *luma = picture.y.data() + static_cast<std::size_t>(sourceRow) * width;
      const std::size_t chromaRow = static_cast<std::size_t>(sourceRow / 2) * chromaWidth;
      const std::uint8_t *cb = picture.cb.data() + chromaRow;
      const std::uint8_t *cr = picture.cr.data() + chromaRow;
      std::uint8_t *write = rgba.data() + static_cast<std::size_t>(row) * width * 4;
      for (int column = 0; column < width; ++column)
      {
        const int y = std::max(0, luma[column] - kYBias);
        const int u = cb[column / 2] - 128;
        const int v = cr[column / 2] - 128;
        const int lum = (kYCoefficient * y) >> 6;
        const int rcr = (kRCrCoefficient * v) >> 6;
        const int gcr = (kGCrCoefficient * v) >> 6;
        const int gcb = (kGCbCoefficient * u) >> 6;
        const int bcb = (kBCbCoefficient * u) >> 6;
        write[0] = clampByte((lum + rcr + 1) >> 1);
        write[1] = clampByte((lum + gcr + gcb + 1) >> 1);
        write[2] = clampByte((lum + bcb + 1) >> 1);
        write[3] = 0xFF;
        write += 4;
      }
    }
  }

} // namespace orphen::harness
