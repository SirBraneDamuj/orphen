#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace orphen::port
{

  class Ps2Memory
  {
  public:
    static constexpr std::size_t kEeRamSize = 32u * 1024u * 1024u;

    explicit Ps2Memory(std::size_t size = kEeRamSize)
        : bytes_(size, 0)
    {
    }

    void clear()
    {
      std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0});
    }

    bool contains(std::uint32_t address, std::size_t byteCount) const
    {
      return address <= bytes_.size() && byteCount <= bytes_.size() - address;
    }

    template <typename T>
    T read(std::uint32_t address) const
    {
      static_assert(std::is_trivially_copyable_v<T>);

      if (!contains(address, sizeof(T)))
      {
        throw std::out_of_range("PS2 memory read outside EE RAM mirror");
      }

      T value;
      std::memcpy(&value, bytes_.data() + address, sizeof(T));
      return value;
    }

    template <typename T>
    void write(std::uint32_t address, const T &value)
    {
      static_assert(std::is_trivially_copyable_v<T>);

      if (!contains(address, sizeof(T)))
      {
        throw std::out_of_range("PS2 memory write outside EE RAM mirror");
      }

      std::memcpy(bytes_.data() + address, &value, sizeof(T));
    }

    std::uint8_t *data() { return bytes_.data(); }
    const std::uint8_t *data() const { return bytes_.data(); }
    std::size_t size() const { return bytes_.size(); }

  private:
    std::vector<std::uint8_t> bytes_;
  };

} // namespace orphen::port
