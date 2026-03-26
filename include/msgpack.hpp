#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <concepts>
#include <cstdint>
#include <deque>
#include <expected>
#include <forward_list>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef MSGPACK_DISABLE_REFLECT
#include "reflect.hpp"
#endif

namespace msgpack {

enum class errc {
  ok = 0,
  need_more_data,
  out_of_range,
  invalid_marker,
  type_mismatch,
  integer_overflow,
  length_overflow,
  unsupported_type,
  unexpected_trailing_bytes,
  invalid_ext,
  not_enough_memory,
};

class error_category final : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "msgpack"; }

  [[nodiscard]] auto message(int condition) const -> std::string override {
    switch (static_cast<errc>(condition)) {
      case errc::ok:
        return "ok";
      case errc::need_more_data:
        return "need more data";
      case errc::out_of_range:
        return "out of range";
      case errc::invalid_marker:
        return "invalid marker";
      case errc::type_mismatch:
        return "type mismatch";
      case errc::integer_overflow:
        return "integer overflow";
      case errc::length_overflow:
        return "length overflow";
      case errc::unsupported_type:
        return "unsupported type";
      case errc::unexpected_trailing_bytes:
        return "unexpected trailing bytes";
      case errc::invalid_ext:
        return "invalid ext payload";
      case errc::not_enough_memory:
        return "not enough memory";
    }

    return "unknown msgpack error";
  }
};

inline const error_category k_error_category{};

[[nodiscard]] inline auto make_error_code(errc code) -> std::error_code {
  return {static_cast<int>(code), k_error_category};
}

struct ext {
  std::int8_t type{};
  std::vector<std::byte> data{};
};

namespace detail {

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool dependent_false_v = false;

[[nodiscard]] inline auto unexpected(errc code)
    -> std::unexpected<std::error_code> {
  return std::unexpected(make_error_code(code));
}

template <typename Fn>
auto guard(Fn&& fn) -> std::expected<void, std::error_code> {
  try {
    fn();
    return {};
  } catch (const std::bad_alloc&) {
    return unexpected(errc::not_enough_memory);
  }
}

template <typename T, typename Fn>
auto guard_value(Fn&& fn) -> std::expected<T, std::error_code> {
  try {
    return fn();
  } catch (const std::bad_alloc&) {
    return unexpected(errc::not_enough_memory);
  }
}

enum class marker : std::uint8_t {
  nil = 0xc0,
  false_bool = 0xc2,
  true_bool = 0xc3,
  bin8 = 0xc4,
  bin16 = 0xc5,
  bin32 = 0xc6,
  ext8 = 0xc7,
  ext16 = 0xc8,
  ext32 = 0xc9,
  float32 = 0xca,
  float64 = 0xcb,
  uint8 = 0xcc,
  uint16 = 0xcd,
  uint32 = 0xce,
  uint64 = 0xcf,
  int8 = 0xd0,
  int16 = 0xd1,
  int32 = 0xd2,
  int64 = 0xd3,
  fixext1 = 0xd4,
  fixext2 = 0xd5,
  fixext4 = 0xd6,
  fixext8 = 0xd7,
  fixext16 = 0xd8,
  str8 = 0xd9,
  str16 = 0xda,
  str32 = 0xdb,
  array16 = 0xdc,
  array32 = 0xdd,
  map16 = 0xde,
  map32 = 0xdf,
};

template <typename T>
concept char_array =
    std::is_array_v<remove_cvref_t<T>> &&
    std::same_as<std::remove_cv_t<std::remove_extent_t<remove_cvref_t<T>>>,
                 char>;

template <typename T>
concept c_string_pointer =
    std::same_as<remove_cvref_t<T>, const char*> ||
    std::same_as<remove_cvref_t<T>, char*>;

template <typename T>
concept string_like =
    std::same_as<remove_cvref_t<T>, std::string> ||
    std::same_as<remove_cvref_t<T>, std::string_view> ||
    c_string_pointer<T> || char_array<T>;

template <typename T>
concept byte_like = std::same_as<remove_cvref_t<T>, std::byte> ||
                    std::same_as<remove_cvref_t<T>, std::uint8_t> ||
                    std::same_as<remove_cvref_t<T>, unsigned char>;

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
concept optional_like = is_optional<remove_cvref_t<T>>::value;

template <typename T>
struct is_time_point : std::false_type {};

template <typename Clock, typename Duration>
struct is_time_point<std::chrono::time_point<Clock, Duration>> : std::true_type {};

template <typename T>
concept time_point_like = is_time_point<remove_cvref_t<T>>::value;

template <typename T>
concept tuple_like = requires { typename std::tuple_size<remove_cvref_t<T>>::type; };

template <typename T>
concept binary_like =
    std::ranges::contiguous_range<remove_cvref_t<T>> &&
    std::ranges::sized_range<remove_cvref_t<T>> &&
    byte_like<std::ranges::range_value_t<remove_cvref_t<T>>> &&
    !string_like<T>;

template <typename T>
concept binary_resizable_like =
    binary_like<T> && std::default_initializable<remove_cvref_t<T>> &&
    requires(remove_cvref_t<T>& value, std::size_t size,
             std::ranges::range_value_t<remove_cvref_t<T>> byte) {
      value.reserve(size);
      value.push_back(byte);
    };

template <typename T>
concept binary_tuple_like = binary_like<T> && tuple_like<T>;

template <typename T>
concept map_like =
    std::default_initializable<remove_cvref_t<T>> &&
    requires(remove_cvref_t<T>& container,
             typename remove_cvref_t<T>::key_type key,
             typename remove_cvref_t<T>::mapped_type mapped) {
      typename remove_cvref_t<T>::key_type;
      typename remove_cvref_t<T>::mapped_type;
      container.emplace(std::move(key), std::move(mapped));
    };

template <typename T>
concept set_like =
    std::default_initializable<remove_cvref_t<T>> &&
    !map_like<T> &&
    requires(remove_cvref_t<T>& container,
             typename remove_cvref_t<T>::value_type value) {
      typename remove_cvref_t<T>::key_type;
      requires std::same_as<typename remove_cvref_t<T>::key_type,
                            typename remove_cvref_t<T>::value_type>;
      container.insert(std::move(value));
    };

template <typename T>
concept sequence_like =
    std::ranges::input_range<remove_cvref_t<T>> &&
    std::default_initializable<remove_cvref_t<T>> &&
    (requires(remove_cvref_t<T>& container,
              typename remove_cvref_t<T>::value_type value) {
       typename remove_cvref_t<T>::value_type;
       container.push_back(std::move(value));
     } ||
     requires(remove_cvref_t<T>& container,
              typename remove_cvref_t<T>::value_type value) {
       typename remove_cvref_t<T>::value_type;
       container.emplace_back(std::move(value));
     } ||
     requires(remove_cvref_t<T>& container,
              typename remove_cvref_t<T>::value_type value) {
       typename remove_cvref_t<T>::value_type;
       container.before_begin();
       container.insert_after(container.before_begin(), std::move(value));
     }) &&
    !string_like<T> && !binary_like<T> && !tuple_like<T> && !set_like<T> &&
    !map_like<T>;

template <typename T>
concept forward_list_like =
    sequence_like<T> &&
    requires(remove_cvref_t<T>& container,
             typename remove_cvref_t<T>::value_type value) {
      container.before_begin();
      container.insert_after(container.before_begin(), std::move(value));
    };

template <typename T>
concept reserveable_container =
    requires(remove_cvref_t<T>& container, std::size_t size) {
      container.reserve(size);
    };

template <typename T>
concept push_back_container =
    requires(remove_cvref_t<T>& container,
             typename remove_cvref_t<T>::value_type value) {
      container.push_back(std::move(value));
    };

template <typename T>
concept emplace_back_container =
    requires(remove_cvref_t<T>& container,
             typename remove_cvref_t<T>::value_type value) {
      container.emplace_back(std::move(value));
    };

template <typename T>
concept insert_or_assign_map =
    map_like<T> &&
    requires(remove_cvref_t<T>& container,
             typename remove_cvref_t<T>::key_type key,
             typename remove_cvref_t<T>::mapped_type mapped) {
      container.insert_or_assign(std::move(key), std::move(mapped));
    };

#ifndef MSGPACK_DISABLE_REFLECT
template <typename T>
concept reflect_record =
    reflect::is_record_like_type_v<remove_cvref_t<T>> && !tuple_like<T>;
#else
template <typename T>
concept reflect_record = false;
#endif

inline auto to_string_view(std::string_view value) -> std::string_view {
  return value;
}

inline auto to_string_view(const std::string& value) -> std::string_view {
  return value;
}

inline auto to_string_view(const char* value) -> std::string_view {
  return value != nullptr ? std::string_view{value} : std::string_view{};
}

template <std::size_t N>
inline auto to_string_view(const char (&value)[N]) -> std::string_view {
  return std::string_view{value, N - 1};
}

template <binary_like T>
auto to_byte_span(const T& value) -> std::span<const std::byte> {
  return std::as_bytes(
      std::span{std::ranges::data(value), std::ranges::size(value)});
}

template <typename To, typename From>
auto checked_integer_cast(From value) -> std::expected<To, std::error_code>
  requires(std::is_integral_v<To> && std::is_integral_v<From>)
{
  if constexpr (std::is_same_v<To, From>) {
    return value;
  } else if constexpr (std::is_signed_v<From> && std::is_signed_v<To>) {
    const auto widened = static_cast<std::intmax_t>(value);
    if (widened < static_cast<std::intmax_t>(std::numeric_limits<To>::min()) ||
        widened > static_cast<std::intmax_t>(std::numeric_limits<To>::max())) {
      return unexpected(errc::integer_overflow);
    }
  } else if constexpr (std::is_signed_v<From> && !std::is_signed_v<To>) {
    if (value < 0) {
      return unexpected(errc::integer_overflow);
    }
    const auto widened = static_cast<std::uintmax_t>(value);
    if (widened > static_cast<std::uintmax_t>(std::numeric_limits<To>::max())) {
      return unexpected(errc::integer_overflow);
    }
  } else if constexpr (!std::is_signed_v<From> && std::is_signed_v<To>) {
    const auto widened = static_cast<std::uintmax_t>(value);
    if (widened > static_cast<std::uintmax_t>(std::numeric_limits<To>::max())) {
      return unexpected(errc::integer_overflow);
    }
  } else if (static_cast<std::uintmax_t>(value) >
             static_cast<std::uintmax_t>(std::numeric_limits<To>::max())) {
    return unexpected(errc::integer_overflow);
  }

  return static_cast<To>(value);
}

[[nodiscard]] constexpr auto is_positive_fixint(std::uint8_t code) -> bool {
  return code <= 0x7f;
}

[[nodiscard]] constexpr auto is_negative_fixint(std::uint8_t code) -> bool {
  return code >= 0xe0;
}

[[nodiscard]] constexpr auto is_fixstr(std::uint8_t code) -> bool {
  return (code & 0xe0u) == 0xa0u;
}

[[nodiscard]] constexpr auto is_fixarray(std::uint8_t code) -> bool {
  return (code & 0xf0u) == 0x90u;
}

[[nodiscard]] constexpr auto is_fixmap(std::uint8_t code) -> bool {
  return (code & 0xf0u) == 0x80u;
}

[[nodiscard]] constexpr auto marker_byte(marker value) -> std::uint8_t {
  return std::to_underlying(value);
}

enum class sized_family {
  string,
  binary,
  array,
  map,
};

template <std::ranges::input_range Range>
[[nodiscard]] auto packed_range_size(const Range& value) -> std::size_t {
  if constexpr (std::ranges::sized_range<Range>) {
    return static_cast<std::size_t>(std::ranges::size(value));
  } else {
    return static_cast<std::size_t>(std::ranges::distance(value));
  }
}

class encoder;
class decoder;

template <typename T>
auto pack_value(encoder& out, const T& value)
    -> std::expected<void, std::error_code>;

template <typename T>
auto unpack_value(decoder& in) -> std::expected<T, std::error_code>;

inline constexpr std::int8_t k_timestamp_ext_type = -1;
inline constexpr std::uint64_t k_timestamp64_seconds_mask =
    (std::uint64_t{1} << 34) - 1;

inline auto append_big_endian(std::vector<std::byte>& out, std::uint32_t value)
    -> std::expected<void, std::error_code> {
  return guard([&] {
    for (int shift = 24; shift >= 0; shift -= 8) {
      out.push_back(
          std::byte{static_cast<std::uint8_t>((value >> shift) & 0xffu)});
    }
  });
}

inline auto append_big_endian(std::vector<std::byte>& out, std::uint64_t value)
    -> std::expected<void, std::error_code> {
  return guard([&] {
    for (int shift = 56; shift >= 0; shift -= 8) {
      out.push_back(
          std::byte{static_cast<std::uint8_t>((value >> shift) & 0xffu)});
    }
  });
}

inline auto read_be_u32(std::span<const std::byte> bytes)
    -> std::expected<std::uint32_t, std::error_code> {
  if (bytes.size() != 4) {
    return unexpected(errc::invalid_ext);
  }

  std::uint32_t value = 0;
  for (const auto byte : bytes) {
    value = (value << 8u) | std::to_integer<std::uint8_t>(byte);
  }
  return value;
}

inline auto read_be_u64(std::span<const std::byte> bytes)
    -> std::expected<std::uint64_t, std::error_code> {
  if (bytes.size() != 8) {
    return unexpected(errc::invalid_ext);
  }

  std::uint64_t value = 0;
  for (const auto byte : bytes) {
    value = (value << 8u) | std::to_integer<std::uint8_t>(byte);
  }
  return value;
}

inline auto decode_timestamp_ext(const ext& value)
    -> std::expected<std::chrono::nanoseconds, std::error_code> {
  if (value.type != k_timestamp_ext_type) {
    return unexpected(errc::type_mismatch);
  }

  if (value.data.size() == 4) {
    auto seconds = read_be_u32(value.data);
    if (!seconds) {
      return std::unexpected(seconds.error());
    }
    return std::chrono::seconds{*seconds};
  }

  if (value.data.size() == 8) {
    auto raw = read_be_u64(value.data);
    if (!raw) {
      return std::unexpected(raw.error());
    }

    const auto nanoseconds = static_cast<std::uint32_t>(*raw >> 34);
    const auto seconds = *raw & k_timestamp64_seconds_mask;
    if (nanoseconds >= 1'000'000'000u) {
      return unexpected(errc::invalid_ext);
    }

    return std::chrono::seconds{seconds} +
           std::chrono::nanoseconds{nanoseconds};
  }

  if (value.data.size() == 12) {
    auto nanoseconds = read_be_u32(std::span<const std::byte>{value.data}.first<4>());
    if (!nanoseconds) {
      return std::unexpected(nanoseconds.error());
    }
    if (*nanoseconds >= 1'000'000'000u) {
      return unexpected(errc::invalid_ext);
    }

    auto seconds_raw =
        read_be_u64(std::span<const std::byte>{value.data}.subspan(4, 8));
    if (!seconds_raw) {
      return std::unexpected(seconds_raw.error());
    }

    const auto seconds = std::bit_cast<std::int64_t>(*seconds_raw);
    return std::chrono::seconds{seconds} +
           std::chrono::nanoseconds{*nanoseconds};
  }

  return unexpected(errc::invalid_ext);
}

template <typename TimePoint>
auto pack_timestamp(encoder& out, const TimePoint& value)
    -> std::expected<void, std::error_code>;

template <typename TimePoint>
auto unpack_timestamp(decoder& in) -> std::expected<TimePoint, std::error_code>;

template <typename T>
auto append_sequence(T& container, typename T::value_type value)
    -> std::expected<void, std::error_code> {
  if constexpr (forward_list_like<T>) {
    return guard([&] {
      auto before = container.before_begin();
      for (auto it = container.begin(); it != container.end(); ++it) {
        before = it;
      }
      container.insert_after(before, std::move(value));
    });
  } else if constexpr (push_back_container<T>) {
    return guard([&] { container.push_back(std::move(value)); });
  } else if constexpr (emplace_back_container<T>) {
    return guard([&] { container.emplace_back(std::move(value)); });
  } else {
    static_assert(dependent_false_v<T>, "Unsupported sequence container");
  }
}

template <typename T>
auto insert_set(T& container, typename T::value_type value)
    -> std::expected<void, std::error_code> {
  return guard([&] { container.insert(std::move(value)); });
}

template <typename T>
auto insert_map(T& container, typename T::key_type key,
                typename T::mapped_type value)
    -> std::expected<void, std::error_code> {
  if constexpr (insert_or_assign_map<T>) {
    return guard(
        [&] { container.insert_or_assign(std::move(key), std::move(value)); });
  } else {
    return guard([&] { container.emplace(std::move(key), std::move(value)); });
  }
}

class encoder {
 public:
  encoder() = default;

  [[nodiscard]] auto finish() && -> std::vector<std::byte> {
    return std::move(bytes_);
  }

  auto write_nil() -> std::expected<void, std::error_code> {
    return push_byte(std::byte{static_cast<std::uint8_t>(marker::nil)});
  }

  auto write_bool(bool value) -> std::expected<void, std::error_code> {
    return push_byte(
        std::byte{value ? static_cast<std::uint8_t>(marker::true_bool)
                        : static_cast<std::uint8_t>(marker::false_bool)});
  }

  template <typename Integer>
  auto write_integer(Integer value) -> std::expected<void, std::error_code>
    requires(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>)
  {
    if constexpr (std::is_signed_v<Integer>) {
      return write_signed(static_cast<std::int64_t>(value));
    } else {
      return write_unsigned(static_cast<std::uint64_t>(value));
    }
  }

  auto write_float(float value) -> std::expected<void, std::error_code> {
    auto status =
        push_byte(std::byte{static_cast<std::uint8_t>(marker::float32)});
    if (!status) {
      return status;
    }
    return push_be(std::bit_cast<std::uint32_t>(value));
  }

  auto write_double(double value) -> std::expected<void, std::error_code> {
    auto status =
        push_byte(std::byte{static_cast<std::uint8_t>(marker::float64)});
    if (!status) {
      return status;
    }
    return push_be(std::bit_cast<std::uint64_t>(value));
  }

  auto write_string(std::string_view value)
      -> std::expected<void, std::error_code> {
    auto status = write_sized_header(sized_family::string, value.size());
    if (!status) {
      return status;
    }
    return append_bytes(std::as_bytes(std::span{value.data(), value.size()}));
  }

  auto write_binary(std::span<const std::byte> value)
      -> std::expected<void, std::error_code> {
    auto status = write_sized_header(sized_family::binary, value.size());
    if (!status) {
      return status;
    }
    return append_bytes(value);
  }

  auto write_array_header(std::size_t size)
      -> std::expected<void, std::error_code> {
    return write_sized_header(sized_family::array, size);
  }

  auto write_map_header(std::size_t size)
      -> std::expected<void, std::error_code> {
    return write_sized_header(sized_family::map, size);
  }

  auto write_ext(const ext& value) -> std::expected<void, std::error_code> {
    auto status = write_ext_header(value.data.size());
    if (!status) {
      return status;
    }
    status = push_byte(std::byte{static_cast<std::uint8_t>(value.type)});
    if (!status) {
      return status;
    }
    return append_bytes(value.data);
  }

 private:
  auto push_byte(std::byte value) -> std::expected<void, std::error_code> {
    return guard([&] { bytes_.push_back(value); });
  }

  auto append_bytes(std::span<const std::byte> value)
      -> std::expected<void, std::error_code> {
    return guard(
        [&] { bytes_.insert(bytes_.end(), value.begin(), value.end()); });
  }

  template <std::integral Integer>
  auto write_tagged(marker type, Integer value)
      -> std::expected<void, std::error_code> {
    auto status = push_byte(std::byte{marker_byte(type)});
    if (!status) {
      return status;
    }
    return push_be(value);
  }

  template <typename Integer>
  auto push_be(Integer value) -> std::expected<void, std::error_code>
    requires(std::is_integral_v<Integer>)
  {
    return guard([&] {
      using unsigned_integer = std::make_unsigned_t<Integer>;
      const auto raw = static_cast<unsigned_integer>(value);
      for (int shift = static_cast<int>(sizeof(unsigned_integer)) - 1;
           shift >= 0; --shift) {
        bytes_.push_back(
            std::byte{static_cast<std::uint8_t>((raw >> (shift * 8)) & 0xffu)});
      }
    });
  }

  auto write_length_header(std::optional<marker> marker8, marker marker16,
                           marker marker32,
                           std::size_t size)
      -> std::expected<void, std::error_code> {
    if (marker8 && size <= std::numeric_limits<std::uint8_t>::max()) {
      return write_tagged(*marker8, static_cast<std::uint8_t>(size));
    }
    if (size <= std::numeric_limits<std::uint16_t>::max()) {
      return write_tagged(marker16, static_cast<std::uint16_t>(size));
    }
    if (size <= std::numeric_limits<std::uint32_t>::max()) {
      return write_tagged(marker32, static_cast<std::uint32_t>(size));
    }
    return unexpected(errc::length_overflow);
  }

  auto write_sized_header(sized_family family, std::size_t size)
      -> std::expected<void, std::error_code> {
    switch (family) {
      case sized_family::string:
        if (size <= 31) {
          return push_byte(std::byte{static_cast<std::uint8_t>(0xa0u | size)});
        }
        return write_length_header(marker::str8, marker::str16,
                                   marker::str32, size);
      case sized_family::binary:
        return write_length_header(marker::bin8, marker::bin16, marker::bin32,
                                   size);
      case sized_family::array:
        if (size <= 0x0f) {
          return push_byte(std::byte{static_cast<std::uint8_t>(0x90u | size)});
        }
        return write_length_header(std::nullopt, marker::array16,
                                   marker::array32, size);
      case sized_family::map:
        if (size <= 0x0f) {
          return push_byte(std::byte{static_cast<std::uint8_t>(0x80u | size)});
        }
        return write_length_header(std::nullopt, marker::map16, marker::map32,
                                   size);
    }
    return unexpected(errc::invalid_marker);
  }

  auto write_ext_header(std::size_t size) -> std::expected<void, std::error_code> {
    switch (size) {
      case 1:
        return push_byte(std::byte{marker_byte(marker::fixext1)});
      case 2:
        return push_byte(std::byte{marker_byte(marker::fixext2)});
      case 4:
        return push_byte(std::byte{marker_byte(marker::fixext4)});
      case 8:
        return push_byte(std::byte{marker_byte(marker::fixext8)});
      case 16:
        return push_byte(std::byte{marker_byte(marker::fixext16)});
      default:
        return write_length_header(marker::ext8, marker::ext16, marker::ext32,
                                   size);
    }
  }

  auto write_unsigned(std::uint64_t value)
      -> std::expected<void, std::error_code> {
    if (value <= 0x7f) {
      return push_byte(std::byte{static_cast<std::uint8_t>(value)});
    }
    if (value <= std::numeric_limits<std::uint8_t>::max()) {
      return write_tagged(marker::uint8, static_cast<std::uint8_t>(value));
    }
    if (value <= std::numeric_limits<std::uint16_t>::max()) {
      return write_tagged(marker::uint16, static_cast<std::uint16_t>(value));
    }
    if (value <= std::numeric_limits<std::uint32_t>::max()) {
      return write_tagged(marker::uint32, static_cast<std::uint32_t>(value));
    }
    return write_tagged(marker::uint64, value);
  }

  auto write_signed(std::int64_t value)
      -> std::expected<void, std::error_code> {
    if (value >= 0) {
      return write_unsigned(static_cast<std::uint64_t>(value));
    }
    if (value >= -32) {
      return push_byte(std::byte{static_cast<std::uint8_t>(value)});
    }
    if (value >= std::numeric_limits<std::int8_t>::min()) {
      return write_tagged(marker::int8, static_cast<std::int8_t>(value));
    }
    if (value >= std::numeric_limits<std::int16_t>::min()) {
      return write_tagged(marker::int16, static_cast<std::int16_t>(value));
    }
    if (value >= std::numeric_limits<std::int32_t>::min()) {
      return write_tagged(marker::int32, static_cast<std::int32_t>(value));
    }
    return write_tagged(marker::int64, value);
  }

  std::vector<std::byte> bytes_{};
};

class decoder {
 public:
  decoder() = default;

  explicit decoder(std::span<const std::byte> bytes)
      : buffer_(bytes.begin(), bytes.end()) {}

  auto feed(std::span<const std::byte> bytes)
      -> std::expected<void, std::error_code> {
    if (bytes.empty()) {
      return {};
    }
    return guard(
        [&] { buffer_.insert(buffer_.end(), bytes.begin(), bytes.end()); });
  }

  auto clear() noexcept -> void {
    buffer_.clear();
    offset_ = 0;
  }

  [[nodiscard]] auto buffered_size() const noexcept -> std::size_t {
    return buffer_.size();
  }

  [[nodiscard]] auto remaining() const noexcept -> std::size_t {
    return buffer_.size() - offset_;
  }
  [[nodiscard]] auto offset() const -> std::size_t { return offset_; }

  [[nodiscard]] auto remaining_bytes() const noexcept
      -> std::span<const std::byte> {
    return std::span<const std::byte>{buffer_}.subspan(offset_);
  }

  auto compact() -> void {
    if (offset_ == 0) {
      return;
    }
    if (offset_ >= buffer_.size()) {
      clear();
      return;
    }
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
    offset_ = 0;
  }

  template <typename T>
  auto unpack() -> std::expected<T, std::error_code> {
    const auto checkpoint = offset_;
    auto value = unpack_value<T>(*this);
    if (!value) {
      if (value.error() == make_error_code(errc::out_of_range)) {
        offset_ = checkpoint;
        return unexpected(errc::need_more_data);
      }
      return std::unexpected(value.error());
    }

    if (offset_ == buffer_.size()) {
      clear();
    } else if (offset_ >= 4096 && offset_ * 2 >= buffer_.size()) {
      compact();
    }
    return value;
  }

  auto restore(std::size_t position) -> std::expected<void, std::error_code> {
    if (position > buffer_.size()) {
      return unexpected(errc::out_of_range);
    }
    offset_ = position;
    return {};
  }

  [[nodiscard]] auto peek() const -> std::expected<std::byte, std::error_code> {
    if (offset_ >= buffer_.size()) {
      return unexpected(errc::out_of_range);
    }
    return buffer_[offset_];
  }

  auto read_nil() -> std::expected<void, std::error_code>;
  auto read_bool() -> std::expected<bool, std::error_code>;
  auto read_string() -> std::expected<std::string, std::error_code>;
  auto read_string_view() -> std::expected<std::string_view, std::error_code>;
  auto read_binary() -> std::expected<std::vector<std::byte>, std::error_code>;
  auto read_binary_view()
      -> std::expected<std::span<const std::byte>, std::error_code>;
  auto read_array_header() -> std::expected<std::size_t, std::error_code>;
  auto read_map_header() -> std::expected<std::size_t, std::error_code>;
  auto read_ext() -> std::expected<ext, std::error_code>;
  auto skip() -> std::expected<void, std::error_code>;

  template <typename Integer>
  auto read_integer() -> std::expected<Integer, std::error_code>
    requires(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>);

  template <typename Float>
  auto read_floating() -> std::expected<Float, std::error_code>
    requires(std::is_floating_point_v<Float>);

 private:
  auto read_byte() -> std::expected<std::byte, std::error_code>;
  auto advance(std::size_t count) -> std::expected<void, std::error_code>;
  auto read_sized_header(sized_family family)
      -> std::expected<std::size_t, std::error_code>;
  auto read_sized_payload(sized_family family, std::uint8_t code)
      -> std::expected<std::size_t, std::error_code>;
  auto read_ext_size(std::uint8_t code)
      -> std::expected<std::size_t, std::error_code>;
  auto skip_array_payload(std::size_t size)
      -> std::expected<void, std::error_code>;
  auto skip_map_payload(std::size_t size)
      -> std::expected<void, std::error_code>;

  template <typename Integer, typename Encoded>
  auto read_integer_payload() -> std::expected<Integer, std::error_code>;

  template <typename Integer, typename Signed, typename Unsigned>
  auto read_signed_integer_payload() -> std::expected<Integer, std::error_code>;

  template <typename Integer>
  auto read_be() -> std::expected<Integer, std::error_code>
    requires(std::is_integral_v<Integer>);

  template <typename Signed, typename Unsigned>
  auto read_signed_be() -> std::expected<Signed, std::error_code>
    requires(std::is_signed_v<Signed> && std::is_unsigned_v<Unsigned> &&
             sizeof(Signed) == sizeof(Unsigned));

  std::vector<std::byte> buffer_{};
  std::size_t offset_{0};
};

template <typename TimePoint>
inline auto pack_timestamp(encoder& out, const TimePoint& value)
    -> std::expected<void, std::error_code> {
  using namespace std::chrono;

  const auto duration = value.time_since_epoch();
  const auto seconds_part = floor<seconds>(duration);
  const auto nanoseconds_part =
      duration_cast<nanoseconds>(duration - seconds_part);

  const auto seconds = seconds_part.count();
  const auto nanoseconds = nanoseconds_part.count();
  if (nanoseconds < 0 || nanoseconds >= 1'000'000'000ll) {
    return unexpected(errc::invalid_ext);
  }

  ext timestamp{};
  timestamp.type = k_timestamp_ext_type;

  if (seconds >= 0 &&
      seconds <=
          static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) &&
      nanoseconds == 0) {
    auto status =
        append_big_endian(timestamp.data, static_cast<std::uint32_t>(seconds));
    if (!status) {
      return status;
    }
    return out.write_ext(timestamp);
  }

  if (seconds >= 0 &&
      static_cast<std::uint64_t>(seconds) <= k_timestamp64_seconds_mask) {
    const auto raw =
        (static_cast<std::uint64_t>(nanoseconds) << 34) |
        static_cast<std::uint64_t>(seconds);
    auto status = append_big_endian(timestamp.data, raw);
    if (!status) {
      return status;
    }
    return out.write_ext(timestamp);
  }

  auto status = append_big_endian(timestamp.data,
                                  static_cast<std::uint32_t>(nanoseconds));
  if (!status) {
    return status;
  }
  status = append_big_endian(timestamp.data,
                             std::bit_cast<std::uint64_t>(
                                 static_cast<std::int64_t>(seconds)));
  if (!status) {
    return status;
  }
  return out.write_ext(timestamp);
}

template <typename TimePoint>
inline auto unpack_timestamp(decoder& in)
    -> std::expected<TimePoint, std::error_code> {
  using namespace std::chrono;

  auto timestamp = in.read_ext();
  if (!timestamp) {
    return std::unexpected(timestamp.error());
  }

  auto total = decode_timestamp_ext(*timestamp);
  if (!total) {
    return std::unexpected(total.error());
  }

  using duration_type = typename TimePoint::duration;
  return TimePoint{duration_cast<duration_type>(*total)};
}

inline auto decoder::read_byte() -> std::expected<std::byte, std::error_code> {
  auto value = peek();
  if (!value) {
    return std::unexpected(value.error());
  }
  ++offset_;
  return *value;
}

inline auto decoder::advance(std::size_t count)
    -> std::expected<void, std::error_code> {
  if (offset_ + count > buffer_.size()) {
    return unexpected(errc::out_of_range);
  }
  offset_ += count;
  return {};
}

template <typename Integer>
inline auto decoder::read_be() -> std::expected<Integer, std::error_code>
  requires(std::is_integral_v<Integer>)
{
  if (offset_ + sizeof(Integer) > buffer_.size()) {
    return unexpected(errc::out_of_range);
  }

  using unsigned_integer = std::make_unsigned_t<Integer>;
  unsigned_integer value{};
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    value = static_cast<unsigned_integer>(
        (value << 8u) | std::to_integer<std::uint8_t>(buffer_[offset_++]));
  }
  return static_cast<Integer>(value);
}

template <typename Signed, typename Unsigned>
inline auto decoder::read_signed_be() -> std::expected<Signed, std::error_code>
  requires(std::is_signed_v<Signed> && std::is_unsigned_v<Unsigned> &&
           sizeof(Signed) == sizeof(Unsigned))
{
  auto raw = read_be<Unsigned>();
  if (!raw) {
    return std::unexpected(raw.error());
  }
  return std::bit_cast<Signed>(*raw);
}

template <typename Integer, typename Encoded>
inline auto decoder::read_integer_payload()
    -> std::expected<Integer, std::error_code> {
  auto decoded = read_be<Encoded>();
  if (!decoded) {
    return std::unexpected(decoded.error());
  }
  return checked_integer_cast<Integer>(*decoded);
}

template <typename Integer, typename Signed, typename Unsigned>
inline auto decoder::read_signed_integer_payload()
    -> std::expected<Integer, std::error_code> {
  auto decoded = read_signed_be<Signed, Unsigned>();
  if (!decoded) {
    return std::unexpected(decoded.error());
  }
  return checked_integer_cast<Integer>(*decoded);
}

inline auto decoder::read_nil() -> std::expected<void, std::error_code> {
  auto value = read_byte();
  if (!value) {
    return std::unexpected(value.error());
  }
  if (*value != std::byte{static_cast<std::uint8_t>(marker::nil)}) {
    return unexpected(errc::type_mismatch);
  }
  return {};
}

inline auto decoder::read_bool() -> std::expected<bool, std::error_code> {
  auto value = read_byte();
  if (!value) {
    return std::unexpected(value.error());
  }
  const auto code = std::to_integer<std::uint8_t>(*value);
  if (code == static_cast<std::uint8_t>(marker::false_bool)) {
    return false;
  }
  if (code == static_cast<std::uint8_t>(marker::true_bool)) {
    return true;
  }
  return unexpected(errc::type_mismatch);
}

template <typename Integer>
inline auto decoder::read_integer() -> std::expected<Integer, std::error_code>
  requires(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>)
{
  auto value = read_byte();
  if (!value) {
    return std::unexpected(value.error());
  }

  const auto code = std::to_integer<std::uint8_t>(*value);
  if (is_positive_fixint(code)) {
    return checked_integer_cast<Integer>(code);
  }
  if (is_negative_fixint(code)) {
    return checked_integer_cast<Integer>(static_cast<std::int8_t>(code));
  }

  switch (static_cast<marker>(code)) {
    case marker::uint8:
      return read_integer_payload<Integer, std::uint8_t>();
    case marker::uint16:
      return read_integer_payload<Integer, std::uint16_t>();
    case marker::uint32:
      return read_integer_payload<Integer, std::uint32_t>();
    case marker::uint64:
      return read_integer_payload<Integer, std::uint64_t>();
    case marker::int8:
      return read_signed_integer_payload<Integer, std::int8_t, std::uint8_t>();
    case marker::int16:
      return read_signed_integer_payload<Integer, std::int16_t,
                                         std::uint16_t>();
    case marker::int32:
      return read_signed_integer_payload<Integer, std::int32_t,
                                         std::uint32_t>();
    case marker::int64:
      return read_signed_integer_payload<Integer, std::int64_t,
                                         std::uint64_t>();
    default:
      return unexpected(errc::type_mismatch);
  }
}

template <typename Float>
inline auto decoder::read_floating() -> std::expected<Float, std::error_code>
  requires(std::is_floating_point_v<Float>)
{
  auto marker_value = peek();
  if (!marker_value) {
    return std::unexpected(marker_value.error());
  }
  const auto code = std::to_integer<std::uint8_t>(*marker_value);

  if (code == static_cast<std::uint8_t>(marker::float32)) {
    auto consumed = read_byte();
    if (!consumed) {
      return std::unexpected(consumed.error());
    }
    auto raw = read_be<std::uint32_t>();
    if (!raw) {
      return std::unexpected(raw.error());
    }
    return static_cast<Float>(std::bit_cast<float>(*raw));
  }
  if (code == static_cast<std::uint8_t>(marker::float64)) {
    auto consumed = read_byte();
    if (!consumed) {
      return std::unexpected(consumed.error());
    }
    auto raw = read_be<std::uint64_t>();
    if (!raw) {
      return std::unexpected(raw.error());
    }
    return static_cast<Float>(std::bit_cast<double>(*raw));
  }

  if (is_positive_fixint(code) || is_negative_fixint(code) ||
      code == static_cast<std::uint8_t>(marker::uint8) ||
      code == static_cast<std::uint8_t>(marker::uint16) ||
      code == static_cast<std::uint8_t>(marker::uint32) ||
      code == static_cast<std::uint8_t>(marker::uint64) ||
      code == static_cast<std::uint8_t>(marker::int8) ||
      code == static_cast<std::uint8_t>(marker::int16) ||
      code == static_cast<std::uint8_t>(marker::int32) ||
      code == static_cast<std::uint8_t>(marker::int64)) {
    const auto checkpoint = offset();
    if (is_negative_fixint(code) ||
        code == static_cast<std::uint8_t>(marker::int8) ||
        code == static_cast<std::uint8_t>(marker::int16) ||
        code == static_cast<std::uint8_t>(marker::int32) ||
        code == static_cast<std::uint8_t>(marker::int64)) {
      auto integer = read_integer<std::int64_t>();
      if (!integer) {
        return std::unexpected(integer.error());
      }
      return static_cast<Float>(*integer);
    }

    auto reset = restore(checkpoint);
    if (!reset) {
      return std::unexpected(reset.error());
    }
    auto integer = read_integer<std::uint64_t>();
    if (!integer) {
      return std::unexpected(integer.error());
    }
    return static_cast<Float>(*integer);
  }

  return unexpected(errc::type_mismatch);
}

inline auto decoder::read_sized_header(sized_family family)
    -> std::expected<std::size_t, std::error_code> {
  auto value = read_byte();
  if (!value) {
    return std::unexpected(value.error());
  }
  return read_sized_payload(family, std::to_integer<std::uint8_t>(*value));
}

inline auto decoder::read_sized_payload(sized_family family, std::uint8_t code)
    -> std::expected<std::size_t, std::error_code> {
  switch (family) {
    case sized_family::string:
      if (is_fixstr(code)) {
        return static_cast<std::size_t>(code & 0x1fu);
      }
      if (code == marker_byte(marker::str8)) {
        auto size = read_be<std::uint8_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      if (code == marker_byte(marker::str16)) {
        auto size = read_be<std::uint16_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      if (code == marker_byte(marker::str32)) {
        auto size = read_be<std::uint32_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      break;
    case sized_family::binary:
      if (code == marker_byte(marker::bin8)) {
        auto size = read_be<std::uint8_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      if (code == marker_byte(marker::bin16)) {
        auto size = read_be<std::uint16_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      if (code == marker_byte(marker::bin32)) {
        auto size = read_be<std::uint32_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      break;
    case sized_family::array:
      if (is_fixarray(code)) {
        return static_cast<std::size_t>(code & 0x0fu);
      }
      if (code == marker_byte(marker::array16)) {
        auto size = read_be<std::uint16_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      if (code == marker_byte(marker::array32)) {
        auto size = read_be<std::uint32_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      break;
    case sized_family::map:
      if (is_fixmap(code)) {
        return static_cast<std::size_t>(code & 0x0fu);
      }
      if (code == marker_byte(marker::map16)) {
        auto size = read_be<std::uint16_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      if (code == marker_byte(marker::map32)) {
        auto size = read_be<std::uint32_t>();
        if (!size) {
          return std::unexpected(size.error());
        }
        return static_cast<std::size_t>(*size);
      }
      break;
  }
  return unexpected(errc::type_mismatch);
}

inline auto decoder::read_string_view()
    -> std::expected<std::string_view, std::error_code> {
  auto size = read_sized_header(sized_family::string);
  if (!size) {
    return std::unexpected(size.error());
  }
  if (offset_ + *size > buffer_.size()) {
    return unexpected(errc::out_of_range);
  }

  const auto* ptr = reinterpret_cast<const char*>(buffer_.data() + offset_);
  const std::string_view out{ptr, *size};
  offset_ += *size;
  return out;
}

inline auto decoder::read_string()
    -> std::expected<std::string, std::error_code> {
  auto view = read_string_view();
  if (!view) {
    return std::unexpected(view.error());
  }
  return guard_value<std::string>([&] { return std::string{*view}; });
}

inline auto decoder::read_binary_view()
    -> std::expected<std::span<const std::byte>, std::error_code> {
  auto size = read_sized_header(sized_family::binary);
  if (!size) {
    return std::unexpected(size.error());
  }
  if (offset_ + *size > buffer_.size()) {
    return unexpected(errc::out_of_range);
  }
  auto out = std::span<const std::byte>{buffer_}.subspan(offset_, *size);
  offset_ += *size;
  return out;
}

inline auto decoder::read_binary()
    -> std::expected<std::vector<std::byte>, std::error_code> {
  auto view = read_binary_view();
  if (!view) {
    return std::unexpected(view.error());
  }
  return guard_value<std::vector<std::byte>>(
      [&] { return std::vector<std::byte>{view->begin(), view->end()}; });
}

inline auto decoder::read_array_header()
    -> std::expected<std::size_t, std::error_code> {
  return read_sized_header(sized_family::array);
}

inline auto decoder::read_map_header()
    -> std::expected<std::size_t, std::error_code> {
  return read_sized_header(sized_family::map);
}

inline auto decoder::read_ext_size(std::uint8_t code)
    -> std::expected<std::size_t, std::error_code> {
  switch (static_cast<marker>(code)) {
    case marker::fixext1:
      return 1;
    case marker::fixext2:
      return 2;
    case marker::fixext4:
      return 4;
    case marker::fixext8:
      return 8;
    case marker::fixext16:
      return 16;
    case marker::ext8: {
      auto decoded = read_be<std::uint8_t>();
      if (!decoded) {
        return std::unexpected(decoded.error());
      }
      return static_cast<std::size_t>(*decoded);
    }
    case marker::ext16: {
      auto decoded = read_be<std::uint16_t>();
      if (!decoded) {
        return std::unexpected(decoded.error());
      }
      return static_cast<std::size_t>(*decoded);
    }
    case marker::ext32: {
      auto decoded = read_be<std::uint32_t>();
      if (!decoded) {
        return std::unexpected(decoded.error());
      }
      return static_cast<std::size_t>(*decoded);
    }
    default:
      return unexpected(errc::type_mismatch);
  }
}

inline auto decoder::read_ext() -> std::expected<ext, std::error_code> {
  auto value = read_byte();
  if (!value) {
    return std::unexpected(value.error());
  }

  auto size = read_ext_size(std::to_integer<std::uint8_t>(*value));
  if (!size) {
    return std::unexpected(size.error());
  }

  auto type = read_signed_be<std::int8_t, std::uint8_t>();
  if (!type) {
    return std::unexpected(type.error());
  }

  if (offset_ + *size > buffer_.size()) {
    return unexpected(errc::out_of_range);
  }

  auto data = guard_value<std::vector<std::byte>>([&] {
    return std::vector<std::byte>{
        buffer_.begin() + static_cast<std::ptrdiff_t>(offset_),
        buffer_.begin() + static_cast<std::ptrdiff_t>(offset_ + *size)};
  });
  if (!data) {
    return std::unexpected(data.error());
  }
  offset_ += *size;
  return ext{*type, std::move(*data)};
}

inline auto decoder::skip_array_payload(std::size_t size)
    -> std::expected<void, std::error_code> {
  for (std::size_t index = 0; index < size; ++index) {
    auto status = skip();
    if (!status) {
      return status;
    }
  }
  return {};
}

inline auto decoder::skip_map_payload(std::size_t size)
    -> std::expected<void, std::error_code> {
  for (std::size_t index = 0; index < size; ++index) {
    auto key = skip();
    if (!key) {
      return key;
    }
    auto mapped = skip();
    if (!mapped) {
      return mapped;
    }
  }
  return {};
}

inline auto decoder::skip() -> std::expected<void, std::error_code> {
  auto value = read_byte();
  if (!value) {
    return std::unexpected(value.error());
  }
  const auto code = std::to_integer<std::uint8_t>(*value);

  if (is_positive_fixint(code) || is_negative_fixint(code) ||
      code == static_cast<std::uint8_t>(marker::nil) ||
      code == static_cast<std::uint8_t>(marker::false_bool) ||
      code == static_cast<std::uint8_t>(marker::true_bool)) {
    return {};
  }
  if (is_fixstr(code)) {
    return advance(code & 0x1fu);
  }
  if (is_fixarray(code)) {
    return skip_array_payload(code & 0x0fu);
  }
  if (is_fixmap(code)) {
    return skip_map_payload(code & 0x0fu);
  }

  switch (static_cast<marker>(code)) {
    case marker::bin8:
    case marker::bin16:
    case marker::bin32: {
      auto size = read_sized_payload(sized_family::binary, code);
      if (!size) return std::unexpected(size.error());
      return advance(*size);
    }
    case marker::float32:
      return advance(4);
    case marker::float64:
      return advance(8);
    case marker::uint8:
    case marker::int8:
      return advance(1);
    case marker::uint16:
    case marker::int16:
      return advance(2);
    case marker::uint32:
    case marker::int32:
      return advance(4);
    case marker::uint64:
    case marker::int64:
      return advance(8);
    case marker::fixext1:
    case marker::fixext2:
    case marker::fixext4:
    case marker::fixext8:
    case marker::fixext16:
    case marker::ext8:
    case marker::ext16:
    case marker::ext32: {
      auto size = read_ext_size(code);
      if (!size) return std::unexpected(size.error());
      return advance(1 + *size);
    }
    case marker::str8:
    case marker::str16:
    case marker::str32: {
      auto size = read_sized_payload(sized_family::string, code);
      if (!size) return std::unexpected(size.error());
      return advance(*size);
    }
    case marker::array16:
    case marker::array32: {
      auto size = read_sized_payload(sized_family::array, code);
      if (!size) return std::unexpected(size.error());
      return skip_array_payload(*size);
    }
    case marker::map16:
    case marker::map32: {
      auto size = read_sized_payload(sized_family::map, code);
      if (!size) return std::unexpected(size.error());
      return skip_map_payload(*size);
    }
    default:
      return unexpected(errc::invalid_marker);
  }
}

template <typename Tuple, std::size_t... Index>
inline auto pack_tuple_like(encoder& out, const Tuple& value,
                            std::index_sequence<Index...>)
    -> std::expected<void, std::error_code> {
  auto status = out.write_array_header(sizeof...(Index));
  if (!status) {
    return status;
  }

  (([&] {
     if (!status) {
       return;
     }
     status = pack_value(out, std::get<Index>(value));
   }()),
   ...);
  return status;
}

template <typename Tuple, std::size_t... Index>
inline auto unpack_tuple_like(decoder& in, std::index_sequence<Index...>)
    -> std::expected<Tuple, std::error_code> {
  auto size = in.read_array_header();
  if (!size) {
    return std::unexpected(size.error());
  }
  if (*size != sizeof...(Index)) {
    return unexpected(errc::type_mismatch);
  }

  Tuple out{};
  std::expected<void, std::error_code> status{};
  (([&] {
     if (!status) {
       return;
     }
     auto value = unpack_value<std::tuple_element_t<Index, Tuple>>(in);
     if (!value) {
       status = std::unexpected(value.error());
       return;
     }
     std::get<Index>(out) = std::move(*value);
   }()),
   ...);
  if (!status) {
    return std::unexpected(status.error());
  }
  return out;
}

#ifndef MSGPACK_DISABLE_REFLECT
template <typename T, std::size_t... Index>
inline auto unpack_aggregate(decoder& in, std::index_sequence<Index...>)
    -> std::expected<T, std::error_code> {
  using tuple_type = std::tuple<reflect::member_type<Index, T>...>;
  auto tuple =
      unpack_tuple_like<tuple_type>(in, std::index_sequence<Index...>{});
  if (!tuple) {
    return std::unexpected(tuple.error());
  }
  return guard_value<T>([&] {
    return std::apply(
        [](auto&&... args) { return T{std::forward<decltype(args)>(args)...}; },
        std::move(*tuple));
  });
}
#endif

template <std::ranges::input_range Range>
inline auto pack_range_as_array(encoder& out, const Range& value)
    -> std::expected<void, std::error_code> {
  auto status = out.write_array_header(packed_range_size(value));
  if (!status) {
    return status;
  }
  for (const auto& element : value) {
    status = pack_value(out, element);
    if (!status) {
      return status;
    }
  }
  return {};
}

template <map_like Map>
inline auto pack_map_entries(encoder& out, const Map& value)
    -> std::expected<void, std::error_code> {
  auto status = out.write_map_header(packed_range_size(value));
  if (!status) {
    return status;
  }
  for (const auto& [key, mapped] : value) {
    status = pack_value(out, key);
    if (!status) {
      return status;
    }
    status = pack_value(out, mapped);
    if (!status) {
      return status;
    }
  }
  return {};
}

template <binary_resizable_like T>
inline auto unpack_binary_resizable(decoder& in)
    -> std::expected<T, std::error_code> {
  auto view = in.read_binary_view();
  if (!view) {
    return std::unexpected(view.error());
  }
  return guard_value<T>([&] {
    T out;
    out.reserve(view->size());
    for (const auto byte : *view) {
      out.push_back(static_cast<typename T::value_type>(
          std::to_integer<std::uint8_t>(byte)));
    }
    return out;
  });
}

template <binary_tuple_like T>
inline auto unpack_binary_tuple(decoder& in) -> std::expected<T, std::error_code> {
  auto view = in.read_binary_view();
  if (!view) {
    return std::unexpected(view.error());
  }

  T out{};
  if (view->size() != std::ranges::size(out)) {
    return unexpected(errc::type_mismatch);
  }

  auto it = view->begin();
  for (auto& element : out) {
    element = static_cast<typename T::value_type>(
        std::to_integer<std::uint8_t>(*it++));
  }
  return out;
}

template <sequence_like T>
inline auto unpack_sequence_container(decoder& in)
    -> std::expected<T, std::error_code> {
  auto size = in.read_array_header();
  if (!size) {
    return std::unexpected(size.error());
  }

  T out{};
  if constexpr (reserveable_container<T>) {
    auto reserve = guard([&] { out.reserve(*size); });
    if (!reserve) {
      return std::unexpected(reserve.error());
    }
  }

  for (std::size_t index = 0; index < *size; ++index) {
    auto value = unpack_value<typename T::value_type>(in);
    if (!value) {
      return std::unexpected(value.error());
    }
    auto status = append_sequence(out, std::move(*value));
    if (!status) {
      return std::unexpected(status.error());
    }
  }
  return out;
}

template <set_like T>
inline auto unpack_set_container(decoder& in) -> std::expected<T, std::error_code> {
  auto size = in.read_array_header();
  if (!size) {
    return std::unexpected(size.error());
  }

  T out{};
  for (std::size_t index = 0; index < *size; ++index) {
    auto value = unpack_value<typename T::value_type>(in);
    if (!value) {
      return std::unexpected(value.error());
    }
    auto status = insert_set(out, std::move(*value));
    if (!status) {
      return std::unexpected(status.error());
    }
  }
  return out;
}

template <map_like T>
inline auto unpack_map_container(decoder& in) -> std::expected<T, std::error_code> {
  auto size = in.read_map_header();
  if (!size) {
    return std::unexpected(size.error());
  }

  T out{};
  for (std::size_t index = 0; index < *size; ++index) {
    auto key = unpack_value<typename T::key_type>(in);
    if (!key) {
      return std::unexpected(key.error());
    }
    auto mapped = unpack_value<typename T::mapped_type>(in);
    if (!mapped) {
      return std::unexpected(mapped.error());
    }
    auto status = insert_map(out, std::move(*key), std::move(*mapped));
    if (!status) {
      return std::unexpected(status.error());
    }
  }
  return out;
}

template <typename T>
inline auto pack_value(encoder& out, const T& value)
    -> std::expected<void, std::error_code> {
  using value_type = remove_cvref_t<T>;

  if constexpr (std::is_same_v<value_type, std::nullptr_t>) {
    return out.write_nil();
  } else if constexpr (std::is_same_v<value_type, bool>) {
    return out.write_bool(value);
  } else if constexpr (std::is_enum_v<value_type>) {
    return out.write_integer(std::to_underlying(value));
  } else if constexpr (std::is_integral_v<value_type>) {
    return out.write_integer(value);
  } else if constexpr (std::is_same_v<value_type, float>) {
    return out.write_float(value);
  } else if constexpr (std::is_same_v<value_type, double>) {
    return out.write_double(value);
  } else if constexpr (time_point_like<value_type>) {
    return pack_timestamp(out, value);
  } else if constexpr (string_like<T>) {
    return out.write_string(to_string_view(value));
  } else if constexpr (binary_like<T>) {
    return out.write_binary(to_byte_span(value));
  } else if constexpr (std::is_same_v<value_type, ext>) {
    return out.write_ext(value);
  } else if constexpr (optional_like<value_type>) {
    if (!value.has_value()) {
      return out.write_nil();
    }
    return pack_value(out, *value);
  } else if constexpr (tuple_like<value_type> && !binary_tuple_like<value_type>) {
    return pack_tuple_like(
        out, value, std::make_index_sequence<std::tuple_size_v<value_type>>{});
  } else if constexpr (sequence_like<value_type> || set_like<value_type>) {
    return pack_range_as_array(out, value);
  } else if constexpr (map_like<value_type>) {
    return pack_map_entries(out, value);
  }
#ifndef MSGPACK_DISABLE_REFLECT
  else if constexpr (reflect_record<value_type>) {
    auto status = out.write_array_header(reflect::size<value_type>());
    if (!status) {
      return status;
    }
    reflect::for_each(
        [&]<auto I>(std::integral_constant<decltype(I), I>) {
          if (!status) {
            return;
          }
          status = pack_value(out, reflect::get<I>(value));
        },
        value);
    return status;
  }
#endif
  else {
    static_assert(dependent_false_v<value_type>,
                  "Unsupported MessagePack type");
  }
}

template <typename T>
inline auto unpack_value(decoder& in) -> std::expected<T, std::error_code> {
  using value_type = remove_cvref_t<T>;

  if constexpr (std::is_same_v<value_type, std::nullptr_t>) {
    auto status = in.read_nil();
    if (!status) {
      return std::unexpected(status.error());
    }
    return nullptr;
  } else if constexpr (std::is_same_v<value_type, bool>) {
    return in.read_bool();
  } else if constexpr (std::is_enum_v<value_type>) {
    auto value = in.read_integer<std::underlying_type_t<value_type>>();
    if (!value) {
      return std::unexpected(value.error());
    }
    return static_cast<value_type>(*value);
  } else if constexpr (std::is_integral_v<value_type>) {
    return in.read_integer<value_type>();
  } else if constexpr (std::is_floating_point_v<value_type>) {
    return in.read_floating<value_type>();
  } else if constexpr (time_point_like<value_type>) {
    return unpack_timestamp<value_type>(in);
  } else if constexpr (std::is_same_v<value_type, std::string>) {
    return in.read_string();
  } else if constexpr (std::is_same_v<value_type, std::string_view>) {
    return in.read_string_view();
  } else if constexpr (binary_resizable_like<value_type>) {
    return unpack_binary_resizable<value_type>(in);
  } else if constexpr (binary_tuple_like<value_type>) {
    return unpack_binary_tuple<value_type>(in);
  } else if constexpr (std::is_same_v<value_type, ext>) {
    return in.read_ext();
  } else if constexpr (optional_like<value_type>) {
    auto marker_value = in.peek();
    if (!marker_value) {
      return std::unexpected(marker_value.error());
    }
    if (*marker_value == std::byte{static_cast<std::uint8_t>(marker::nil)}) {
      auto status = in.read_nil();
      if (!status) {
        return std::unexpected(status.error());
      }
      return value_type{};
    }
    auto inner = unpack_value<typename value_type::value_type>(in);
    if (!inner) {
      return std::unexpected(inner.error());
    }
    return value_type{std::move(*inner)};
  } else if constexpr (tuple_like<value_type> && !binary_tuple_like<value_type>) {
    return unpack_tuple_like<value_type>(
        in, std::make_index_sequence<std::tuple_size_v<value_type>>{});
  } else if constexpr (sequence_like<value_type>) {
    return unpack_sequence_container<value_type>(in);
  } else if constexpr (set_like<value_type>) {
    return unpack_set_container<value_type>(in);
  } else if constexpr (map_like<value_type>) {
    return unpack_map_container<value_type>(in);
  }
#ifndef MSGPACK_DISABLE_REFLECT
  else if constexpr (reflect_record<value_type>) {
    return unpack_aggregate<value_type>(
        in, std::make_index_sequence<reflect::size<value_type>()>{});
  }
#endif
  else {
    static_assert(dependent_false_v<value_type>,
                  "Unsupported MessagePack type");
  }
}

}  // namespace detail

using writer = detail::encoder;
using reader = detail::decoder;

template <typename T>
inline auto pack(writer& out, const T& value)
    -> std::expected<void, std::error_code> {
  return detail::pack_value(out, value);
}

template <typename T>
inline auto unpack(reader& in) -> std::expected<T, std::error_code> {
  return in.template unpack<T>();
}

template <typename T>
inline auto pack(const T& value)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  writer out;
  auto status = pack(out, value);
  if (!status) {
    return std::unexpected(status.error());
  }
  return std::move(out).finish();
}

template <typename T>
inline auto unpack(std::span<const std::byte> bytes)
    -> std::expected<T, std::error_code> {
  reader in{bytes};
  auto value = unpack<T>(in);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (in.remaining() != 0) {
    return detail::unexpected(errc::unexpected_trailing_bytes);
  }
  return value;
}

template <typename T>
inline auto unpack(const std::vector<std::byte>& bytes)
    -> std::expected<T, std::error_code> {
  return unpack<T>(std::span<const std::byte>{bytes});
}

}  // namespace msgpack

namespace std {

template <>
struct is_error_code_enum<msgpack::errc> : true_type {};

}  // namespace std
