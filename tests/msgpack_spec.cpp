#include "msgpack.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using bytes = std::vector<std::byte>;
using sys_nanos = std::chrono::sys_time<std::chrono::nanoseconds>;

[[noreturn]] auto fail(std::string_view message,
                       std::source_location location =
                           std::source_location::current()) -> void {
  std::cerr << location.file_name() << ':' << location.line() << ": " << message
            << '\n';
  std::exit(EXIT_FAILURE);
}

auto check(bool condition, std::string_view message,
           std::source_location location = std::source_location::current())
    -> void {
  if (!condition) {
    fail(message, location);
  }
}

template <typename Actual, typename Expected>
auto check_equal(const Actual& actual, const Expected& expected,
                 std::string_view message = "values differ",
                 std::source_location location =
                     std::source_location::current()) -> void {
  if (!(actual == expected)) {
    fail(message, location);
  }
}

template <typename T>
auto expect_error(const std::expected<T, std::error_code>& result,
                  msgpack::errc code, std::string_view message,
                  std::source_location location =
                      std::source_location::current()) -> void {
  if (result.has_value()) {
    fail(message, location);
  }
  check_equal(result.error(), msgpack::make_error_code(code), message, location);
}

auto expect_error(const std::expected<void, std::error_code>& result,
                  msgpack::errc code, std::string_view message,
                  std::source_location location =
                      std::source_location::current()) -> void {
  if (result.has_value()) {
    fail(message, location);
  }
  check_equal(result.error(), msgpack::make_error_code(code), message, location);
}

auto make_bytes(std::initializer_list<unsigned int> values) -> bytes {
  bytes out;
  out.reserve(values.size());
  for (const auto value : values) {
    out.push_back(std::byte{static_cast<std::uint8_t>(value)});
  }
  return out;
}

auto make_repeated_bytes(std::size_t size, std::uint8_t value) -> bytes {
  return bytes(size, std::byte{value});
}

auto make_string(std::size_t size, char value = 'x') -> std::string {
  return std::string(size, value);
}

auto append_u16_be(bytes& out, std::uint16_t value) -> void {
  out.push_back(std::byte{static_cast<std::uint8_t>((value >> 8u) & 0xffu)});
  out.push_back(std::byte{static_cast<std::uint8_t>(value & 0xffu)});
}

auto append_u32_be(bytes& out, std::uint32_t value) -> void {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(
        std::byte{static_cast<std::uint8_t>((value >> shift) & 0xffu)});
  }
}

auto append_u64_be(bytes& out, std::uint64_t value) -> void {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(
        std::byte{static_cast<std::uint8_t>((value >> shift) & 0xffu)});
  }
}

template <typename... Ranges>
auto concat_bytes(const Ranges&... ranges) -> bytes {
  bytes out;
  (out.insert(out.end(), std::begin(ranges), std::end(ranges)), ...);
  return out;
}

auto check_prefix(std::span<const std::byte> actual,
                  std::initializer_list<unsigned int> expected,
                  std::string_view message = "unexpected prefix",
                  std::source_location location =
                      std::source_location::current()) -> void {
  check(actual.size() >= expected.size(), message, location);
  std::size_t index = 0;
  for (const auto value : expected) {
    if (actual[index] != std::byte{static_cast<std::uint8_t>(value)}) {
      fail(message, location);
    }
    ++index;
  }
}

template <typename T>
auto pack_checked(const T& value) -> bytes {
  auto result = msgpack::pack(value);
  if (!result) {
    fail(result.error().message());
  }
  return std::move(*result);
}

template <typename T>
auto unpack_checked(std::span<const std::byte> encoded) -> T {
  auto result = msgpack::unpack<T>(encoded);
  if (!result) {
    fail(result.error().message());
  }
  return std::move(*result);
}

template <typename T>
auto expect_pack_bytes(const T& value, const bytes& expected,
                       std::string_view message = "unexpected encoded bytes")
    -> void {
  check_equal(pack_checked(value), expected, message);
}

template <typename T>
auto expect_unpack_value(const bytes& encoded, const T& expected,
                         std::string_view message = "unexpected decoded value")
    -> void {
  check_equal(unpack_checked<T>(encoded), expected, message);
}

auto make_uint_map(std::size_t size) -> std::map<std::uint32_t, std::uint32_t> {
  std::map<std::uint32_t, std::uint32_t> out;
  for (std::uint32_t index = 0; index < size; ++index) {
    out.emplace(index, index);
  }
  return out;
}

auto make_ext(std::int8_t type, bytes data) -> msgpack::ext {
  return msgpack::ext{type, std::move(data)};
}

auto test_nil_bool() -> void {
  expect_pack_bytes(nullptr, make_bytes({0xc0}), "nil must encode to 0xc0");
  expect_pack_bytes(false, make_bytes({0xc2}), "false must encode to 0xc2");
  expect_pack_bytes(true, make_bytes({0xc3}), "true must encode to 0xc3");

  expect_unpack_value<bool>(make_bytes({0xc2}), false);
  expect_unpack_value<bool>(make_bytes({0xc3}), true);

  auto nil = msgpack::unpack<std::nullptr_t>(make_bytes({0xc0}));
  check(nil.has_value(), "nil must decode");

  expect_unpack_value<std::optional<int>>(make_bytes({0xc0}), std::nullopt);
  expect_unpack_value<std::optional<int>>(make_bytes({0x2a}), std::optional{42});
}

auto test_integers() -> void {
  expect_pack_bytes(std::uint64_t{0}, make_bytes({0x00}));
  expect_pack_bytes(std::uint64_t{127}, make_bytes({0x7f}));
  expect_pack_bytes(std::uint64_t{128}, make_bytes({0xcc, 0x80}));
  expect_pack_bytes(std::uint64_t{255}, make_bytes({0xcc, 0xff}));
  expect_pack_bytes(std::uint64_t{256}, make_bytes({0xcd, 0x01, 0x00}));
  expect_pack_bytes(std::uint64_t{65536},
                    make_bytes({0xce, 0x00, 0x01, 0x00, 0x00}));
  expect_pack_bytes(std::uint64_t{std::numeric_limits<std::uint64_t>::max()},
                    make_bytes({0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff}));

  expect_pack_bytes(std::int64_t{-1}, make_bytes({0xff}));
  expect_pack_bytes(std::int64_t{-32}, make_bytes({0xe0}));
  expect_pack_bytes(std::int64_t{-33}, make_bytes({0xd0, 0xdf}));
  expect_pack_bytes(std::int64_t{std::numeric_limits<std::int8_t>::min()},
                    make_bytes({0xd0, 0x80}));
  expect_pack_bytes(std::int64_t{std::numeric_limits<std::int16_t>::min()},
                    make_bytes({0xd1, 0x80, 0x00}));
  expect_pack_bytes(std::int64_t{std::numeric_limits<std::int32_t>::min()},
                    make_bytes({0xd2, 0x80, 0x00, 0x00, 0x00}));
  expect_pack_bytes(std::numeric_limits<std::int64_t>::min(),
                    make_bytes({0xd3, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00}));

  expect_unpack_value<std::int64_t>(make_bytes({0xe0}), -32);
  expect_unpack_value<std::uint64_t>(make_bytes({0xcc, 0xff}), 255);
  expect_unpack_value<std::uint64_t>(make_bytes({0xcd, 0x01, 0x00}), 256);
  expect_unpack_value<std::int64_t>(make_bytes({0xd1, 0xff, 0x7f}), -129);
  expect_unpack_value<std::int64_t>(
      make_bytes({0xd3, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
      std::numeric_limits<std::int64_t>::min());

  expect_error(msgpack::unpack<std::int8_t>(make_bytes({0xcc, 0x80})),
               msgpack::errc::integer_overflow,
               "uint8 128 must not fit into int8");
  expect_error(msgpack::unpack<std::uint8_t>(make_bytes({0xff})),
               msgpack::errc::integer_overflow,
               "negative fixint must not fit into uint8");
  expect_error(
      msgpack::unpack<std::int64_t>(
          make_bytes({0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})),
      msgpack::errc::integer_overflow,
      "uint64 max must not fit into int64");
}

auto test_floats() -> void {
  expect_pack_bytes(1.0f, make_bytes({0xca, 0x3f, 0x80, 0x00, 0x00}));
  expect_pack_bytes(1.0,
                    make_bytes({0xcb, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00}));

  check_equal(unpack_checked<double>(make_bytes({0xca, 0x3f, 0xc0, 0x00, 0x00})),
              1.5, "float32 must decode to double");
  check_equal(
      unpack_checked<float>(make_bytes({0xcb, 0x3f, 0xf4, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00})),
      1.25f, "float64 must decode to float");

  auto nan =
      unpack_checked<double>(make_bytes({0xcb, 0x7f, 0xf8, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00}));
  check(std::isnan(nan), "NaN must remain NaN");

  auto inf =
      unpack_checked<double>(make_bytes({0xcb, 0x7f, 0xf0, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00}));
  check(std::isinf(inf) && inf > 0, "infinity must decode");
}

auto test_strings() -> void {
  expect_pack_bytes(std::string{}, make_bytes({0xa0}));

  const auto s31 = make_string(31, 'a');
  const auto s32 = make_string(32, 'b');
  const auto s255 = make_string(255, 'c');
  const auto s256 = make_string(256, 'd');
  const auto s65535 = make_string(65535, 'e');
  const auto s65536 = make_string(65536, 'f');

  const auto p31 = pack_checked(s31);
  check_prefix(p31, {0xbf}, "fixstr header expected");
  check_equal(p31.size(), std::size_t{32}, "fixstr payload size mismatch");
  expect_unpack_value<std::string>(p31, s31);

  const auto p32 = pack_checked(s32);
  check_prefix(p32, {0xd9, 0x20}, "str8 header expected");
  expect_unpack_value<std::string>(p32, s32);

  const auto p255 = pack_checked(s255);
  check_prefix(p255, {0xd9, 0xff}, "str8 max header expected");
  expect_unpack_value<std::string>(p255, s255);

  const auto p256 = pack_checked(s256);
  check_prefix(p256, {0xda, 0x01, 0x00}, "str16 header expected");
  expect_unpack_value<std::string>(p256, s256);

  const auto p65535 = pack_checked(s65535);
  check_prefix(p65535, {0xda, 0xff, 0xff}, "str16 max header expected");
  expect_unpack_value<std::string>(p65535, s65535);

  const auto p65536 = pack_checked(s65536);
  check_prefix(p65536, {0xdb, 0x00, 0x01, 0x00, 0x00},
               "str32 header expected");
  expect_unpack_value<std::string>(p65536, s65536);

  expect_pack_bytes(std::string_view{"hi"}, make_bytes({0xa2, 0x68, 0x69}));
}

auto test_binary() -> void {
  const bytes empty{};
  expect_pack_bytes(empty, make_bytes({0xc4, 0x00}));

  const auto b4 = make_repeated_bytes(4, 0xaa);
  const auto b255 = make_repeated_bytes(255, 0xbb);
  const auto b256 = make_repeated_bytes(256, 0xcc);
  const auto b65536 = make_repeated_bytes(65536, 0xdd);

  expect_pack_bytes(std::array<std::byte, 4>{std::byte{0xaa}, std::byte{0xaa},
                                             std::byte{0xaa}, std::byte{0xaa}},
                    make_bytes({0xc4, 0x04, 0xaa, 0xaa, 0xaa, 0xaa}));

  const auto p255 = pack_checked(b255);
  check_prefix(p255, {0xc4, 0xff}, "bin8 header expected");
  expect_unpack_value<std::vector<std::byte>>(p255, b255);

  const auto p256 = pack_checked(b256);
  check_prefix(p256, {0xc5, 0x01, 0x00}, "bin16 header expected");
  expect_unpack_value<std::vector<std::byte>>(p256, b256);

  const auto p65536 = pack_checked(b65536);
  check_prefix(p65536, {0xc6, 0x00, 0x01, 0x00, 0x00},
               "bin32 header expected");
  expect_unpack_value<std::vector<std::byte>>(p65536, b65536);

  expect_unpack_value<std::array<std::byte, 4>>(pack_checked(std::array{
                            std::byte{0x10}, std::byte{0x20}, std::byte{0x30},
                            std::byte{0x40}}),
                        std::array{std::byte{0x10}, std::byte{0x20},
                                   std::byte{0x30}, std::byte{0x40}});

  expect_pack_bytes(std::span<const std::byte>{b4}, make_bytes({0xc4, 0x04, 0xaa,
                                                                0xaa, 0xaa,
                                                                0xaa}));
}

auto test_arrays() -> void {
  expect_pack_bytes(std::pair{1, 2}, make_bytes({0x92, 0x01, 0x02}));
  expect_pack_bytes(std::tuple{1, true, std::string{"a"}},
                    make_bytes({0x93, 0x01, 0xc3, 0xa1, 0x61}));
  expect_pack_bytes(std::array{1, 2, 3}, make_bytes({0x93, 0x01, 0x02, 0x03}));

  const std::vector<int> a15(15, 0);
  const std::vector<int> a16(16, 0);
  const std::vector<int> a65536(65536, 0);

  const auto p15 = pack_checked(a15);
  check_prefix(p15, {0x9f}, "fixarray header expected");
  expect_unpack_value<std::vector<int>>(p15, a15);

  const auto p16 = pack_checked(a16);
  check_prefix(p16, {0xdc, 0x00, 0x10}, "array16 header expected");
  expect_unpack_value<std::vector<int>>(p16, a16);

  const auto p65536 = pack_checked(a65536);
  check_prefix(p65536, {0xdd, 0x00, 0x01, 0x00, 0x00},
               "array32 header expected");
  const auto decoded = unpack_checked<std::vector<int>>(p65536);
  check_equal(decoded.size(), std::size_t{65536}, "array32 size mismatch");
  check(std::ranges::all_of(decoded, [](int value) { return value == 0; }),
        "array32 values mismatch");
}

auto test_maps() -> void {
  expect_pack_bytes(std::map<int, int>{}, make_bytes({0x80}));
  expect_pack_bytes(std::map<int, int>{{1, 2}, {3, 4}},
                    make_bytes({0x82, 0x01, 0x02, 0x03, 0x04}));

  const auto m15 = make_uint_map(15);
  const auto m16 = make_uint_map(16);
  const auto m65536 = make_uint_map(65536);

  const auto p15 = pack_checked(m15);
  check_prefix(p15, {0x8f}, "fixmap header expected");
  expect_unpack_value<std::map<std::uint32_t, std::uint32_t>>(p15, m15);

  const auto p16 = pack_checked(m16);
  check_prefix(p16, {0xde, 0x00, 0x10}, "map16 header expected");
  expect_unpack_value<std::map<std::uint32_t, std::uint32_t>>(p16, m16);

  const auto p65536 = pack_checked(m65536);
  check_prefix(p65536, {0xdf, 0x00, 0x01, 0x00, 0x00},
               "map32 header expected");
  const auto decoded = unpack_checked<std::map<std::uint32_t, std::uint32_t>>(
      p65536);
  check_equal(decoded.size(), std::size_t{65536}, "map32 size mismatch");
  check_equal(decoded.begin()->first, std::uint32_t{0}, "map32 first key mismatch");
  check_equal(decoded.rbegin()->first, std::uint32_t{65535},
              "map32 last key mismatch");
}

auto test_ext() -> void {
  expect_pack_bytes(make_ext(std::int8_t{1}, make_bytes({0xaa})),
                    make_bytes({0xd4, 0x01, 0xaa}));
  expect_pack_bytes(make_ext(static_cast<std::int8_t>(-2), make_bytes({0xaa, 0xbb})),
                    make_bytes({0xd5, 0xfe, 0xaa, 0xbb}));
  expect_pack_bytes(make_ext(std::int8_t{3}, make_bytes({1, 2, 3, 4})),
                    make_bytes({0xd6, 0x03, 0x01, 0x02, 0x03, 0x04}));
  expect_pack_bytes(make_ext(std::int8_t{4}, make_bytes({1, 2, 3, 4, 5, 6, 7, 8})),
                    make_bytes(
                        {0xd7, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

  const auto ext16 = make_ext(std::int8_t{5}, make_repeated_bytes(16, 0xee));
  const auto p16 = pack_checked(ext16);
  check_prefix(p16, {0xd8, 0x05}, "fixext16 header expected");
  const auto d16 = unpack_checked<msgpack::ext>(p16);
  check_equal(d16.type, ext16.type, "fixext16 type mismatch");
  check_equal(d16.data, ext16.data, "fixext16 payload mismatch");

  const auto ext8 = make_ext(std::int8_t{6}, make_repeated_bytes(17, 0xab));
  const auto p8 = pack_checked(ext8);
  check_prefix(p8, {0xc7, 0x11, 0x06}, "ext8 header expected");
  const auto d8 = unpack_checked<msgpack::ext>(p8);
  check_equal(d8.type, ext8.type, "ext8 type mismatch");
  check_equal(d8.data, ext8.data, "ext8 payload mismatch");

  const auto ext16v =
      make_ext(static_cast<std::int8_t>(-7), make_repeated_bytes(256, 0xcd));
  const auto p16v = pack_checked(ext16v);
  check_prefix(p16v, {0xc8, 0x01, 0x00, 0xf9}, "ext16 header expected");
  const auto d16v = unpack_checked<msgpack::ext>(p16v);
  check_equal(d16v.type, ext16v.type, "ext16 type mismatch");
  check_equal(d16v.data, ext16v.data, "ext16 payload mismatch");

  const auto ext32 = make_ext(std::int8_t{8}, make_repeated_bytes(65536, 0xef));
  const auto p32 = pack_checked(ext32);
  check_prefix(p32, {0xc9, 0x00, 0x01, 0x00, 0x00, 0x08},
               "ext32 header expected");
  const auto d32 = unpack_checked<msgpack::ext>(p32);
  check_equal(d32.type, ext32.type, "ext32 type mismatch");
  check_equal(d32.data, ext32.data, "ext32 payload mismatch");
}

auto test_timestamp() -> void {
  const auto ts32 = sys_nanos{std::chrono::seconds{1}};
  const auto ts64 =
      sys_nanos{std::chrono::seconds{1} + std::chrono::nanoseconds{2}};
  const auto ts96 =
      sys_nanos{std::chrono::seconds{-1} + std::chrono::nanoseconds{123456789}};

  expect_pack_bytes(ts32, make_bytes({0xd6, 0xff, 0x00, 0x00, 0x00, 0x01}));
  expect_unpack_value<sys_nanos>(make_bytes({0xd6, 0xff, 0x00, 0x00, 0x00, 0x01}),
                                 ts32);

  expect_pack_bytes(
      ts64, make_bytes({0xd7, 0xff, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01}));
  expect_unpack_value<sys_nanos>(
      make_bytes({0xd7, 0xff, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01}),
      ts64);

  expect_pack_bytes(ts96,
                    make_bytes({0xc7, 0x0c, 0xff, 0x07, 0x5b, 0xcd, 0x15,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff}));
  expect_unpack_value<sys_nanos>(
      make_bytes({0xc7, 0x0c, 0xff, 0x07, 0x5b, 0xcd, 0x15, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff, 0xff, 0xff}),
      ts96);

  bytes invalid = make_bytes({0xd7, 0xff});
  append_u64_be(invalid, (std::uint64_t{1'000'000'000} << 34u));
  expect_error(msgpack::unpack<sys_nanos>(invalid), msgpack::errc::invalid_ext,
               "timestamp nanoseconds must be below 1e9");
}

auto test_reader_behavior() -> void {
  msgpack::reader truncated{std::span<const std::byte>{make_bytes({0xd9, 0x03, 0x61, 0x62})}};
  expect_error(truncated.unpack<std::string>(), msgpack::errc::need_more_data,
               "truncated str payload must request more data");

  expect_error(msgpack::unpack<int>(concat_bytes(pack_checked(1), pack_checked(2))),
               msgpack::errc::unexpected_trailing_bytes,
               "extra bytes must be rejected");

  const auto combined =
      concat_bytes(pack_checked(std::vector<int>{1, 2, 3}),
                   pack_checked(std::string{"ok"}));
  msgpack::reader reader{std::span<const std::byte>{combined}};
  auto skipped = reader.skip();
  if (!skipped) {
    fail(skipped.error().message());
  }
  auto next = reader.unpack<std::string>();
  if (!next) {
    fail(next.error().message());
  }
  check_equal(*next, std::string{"ok"}, "skip must leave next object readable");

  msgpack::reader invalid{std::span<const std::byte>{make_bytes({0xc1})}};
  expect_error(invalid.skip(), msgpack::errc::invalid_marker,
               "reserved marker 0xc1 must be rejected");

  msgpack::reader nested{std::span<const std::byte>{make_bytes(
      {0x92, 0x93, 0x01, 0x02, 0x03, 0x81, 0xa1, 0x61, 0x2a, 0x07})}};
  auto skip_nested = nested.skip();
  if (!skip_nested) {
    fail(skip_nested.error().message());
  }
  auto remaining = nested.unpack<int>();
  if (!remaining) {
    fail(remaining.error().message());
  }
  check_equal(*remaining, 7, "skip must handle nested objects");
}

struct test_case {
  std::string_view name;
  void (*fn)();
};

constexpr std::array k_tests{
    test_case{"nil-bool", test_nil_bool},
    test_case{"integers", test_integers},
    test_case{"floats", test_floats},
    test_case{"strings", test_strings},
    test_case{"binary", test_binary},
    test_case{"arrays", test_arrays},
    test_case{"maps", test_maps},
    test_case{"ext", test_ext},
    test_case{"timestamp", test_timestamp},
    test_case{"reader-behavior", test_reader_behavior},
};

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc == 1) {
    for (const auto& test : k_tests) {
      std::cout << "running " << test.name << '\n';
      test.fn();
    }
    return EXIT_SUCCESS;
  }

  const std::string_view requested = argv[1];
  for (const auto& test : k_tests) {
    if (test.name == requested) {
      test.fn();
      return EXIT_SUCCESS;
    }
  }

  std::cerr << "unknown test suite: " << requested << '\n';
  return EXIT_FAILURE;
}
