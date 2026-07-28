#include "ported/resource/headerless_lz_decoder.h"

#include <stdexcept>

namespace orphen::ported::resource
{
  namespace
  {

    std::uint8_t readByte(std::span<const std::uint8_t> source, std::size_t &inputOffset)
    {
      if (inputOffset >= source.size())
      {
        throw std::runtime_error("unexpected end of headerless LZ input stream");
      }
      return source[inputOffset++];
    }

    void appendRepeated(std::vector<std::uint8_t> &output, std::uint8_t value, std::size_t count)
    {
      output.insert(output.end(), count, value);
    }

    void appendBackReference(std::vector<std::uint8_t> &output, std::uint16_t displacement, std::size_t count)
    {
      if (displacement == 0 || displacement > output.size())
      {
        throw std::runtime_error("invalid headerless LZ back-reference displacement");
      }

      std::size_t sourceOffset = output.size() - displacement;
      for (std::size_t byteIndex = 0; byteIndex < count; ++byteIndex)
      {
        output.push_back(output[sourceOffset++]);
      }
    }

  } // namespace

  std::vector<std::uint8_t> decodeHeaderlessLzStream(std::span<const std::uint8_t> source)
  {
    std::vector<std::uint8_t> output;
    std::size_t inputOffset = 0;

    while (true)
    {
      const std::uint8_t control = readByte(source, inputOffset);
      if (control == 0)
      {
        return output;
      }

      if ((control & 0x80) != 0)
      {
        const std::uint8_t lowDisplacement = readByte(source, inputOffset);
        const std::uint16_t displacement = static_cast<std::uint16_t>(((control & 0x1f) << 8) | lowDisplacement);
        appendBackReference(output, displacement, (control >> 5) | 4);

        while (inputOffset < source.size() && (source[inputOffset] & 0xe0) == 0x60)
        {
          const std::uint8_t continuation = readByte(source, inputOffset);
          appendBackReference(output, displacement, continuation & 0x1f);
        }
        continue;
      }

      if ((control & 0x40) != 0)
      {
        std::size_t count = control & 0x0f;
        if ((control & 0x10) != 0)
        {
          count = (count << 8) | readByte(source, inputOffset);
        }
        appendRepeated(output, readByte(source, inputOffset), count + 4);
        continue;
      }

      std::size_t count = control & 0x1f;
      if ((control & 0x20) != 0)
      {
        count = (count << 8) | readByte(source, inputOffset);
      }
      for (std::size_t byteIndex = 0; byteIndex < count; ++byteIndex)
      {
        output.push_back(readByte(source, inputOffset));
      }
    }
  }

} // namespace orphen::ported::resource
