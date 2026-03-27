#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <expected>
#include <functional>
#include <iostream>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "msgpack_rpc.hpp"

namespace {

using bytes = std::vector<std::byte>;

[[noreturn]] auto fail(
    std::string_view message,
    std::source_location location = std::source_location::current()) -> void {
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
auto check_equal(
    const Actual& actual, const Expected& expected,
    std::string_view message = "values differ",
    std::source_location location = std::source_location::current()) -> void {
  if (!(actual == expected)) {
    fail(message, location);
  }
}

template <typename T>
auto expect_error(
    const std::expected<T, std::error_code>& result, std::error_code code,
    std::string_view message,
    std::source_location location = std::source_location::current()) -> void {
  if (result.has_value()) {
    fail(message, location);
  }
  check_equal(result.error(), code, message, location);
}

auto expect_error(
    const std::expected<void, std::error_code>& result, std::error_code code,
    std::string_view message,
    std::source_location location = std::source_location::current()) -> void {
  if (result.has_value()) {
    fail(message, location);
  }
  check_equal(result.error(), code, message, location);
}

auto make_bytes(std::initializer_list<unsigned int> values) -> bytes {
  bytes out;
  out.reserve(values.size());
  for (const auto value : values) {
    out.push_back(std::byte{static_cast<std::uint8_t>(value)});
  }
  return out;
}

template <typename... T>
auto pack_checked(T&&... value) -> bytes {
  auto result = msgpack::pack(std::forward<T>(value)...);
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

struct scripted_transport {
  using endpoint_type = std::string;

  std::string endpoint{};
  std::deque<bytes> incoming{};
  std::vector<bytes> outgoing{};

  auto connect(const endpoint_type& endpoint)
      -> std::expected<void, std::error_code> {
    this->endpoint = endpoint;
    return {};
  }

  auto close() -> std::expected<void, std::error_code> {
    endpoint.clear();
    return {};
  }

  auto send(std::span<const std::byte> bytes)
      -> std::expected<void, std::error_code> {
    outgoing.emplace_back(bytes.begin(), bytes.end());
    return {};
  }

  auto receive()
      -> std::expected<std::optional<std::vector<std::byte>>, std::error_code> {
    if (incoming.empty()) {
      return std::optional<std::vector<std::byte>>{};
    }

    auto bytes = std::move(incoming.front());
    incoming.pop_front();
    return std::optional<std::vector<std::byte>>{std::move(bytes)};
  }
};

template <typename... Args>
auto make_request(std::uint32_t msgid, std::string_view method, Args&&... args)
    -> bytes {
  return pack_checked(std::tuple{
      msgpack::rpc::k_request_type, msgid, std::string{method},
      std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}});
}

template <typename... Args>
auto make_notification(std::string_view method, Args&&... args) -> bytes {
  return pack_checked(std::tuple{
      msgpack::rpc::k_notification_type, std::string{method},
      std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}});
}

template <typename Error, typename Result>
auto make_response(std::uint32_t msgid, Error&& error, Result&& result)
    -> bytes {
  return pack_checked(std::tuple{msgpack::rpc::k_response_type, msgid,
                                 std::forward<Error>(error),
                                 std::forward<Result>(result)});
}

auto open_client() -> msgpack::rpc::client<scripted_transport> {
  msgpack::rpc::client<scripted_transport> client{};
  auto opened = client.connect("loopback://rpc");
  if (!opened) {
    fail(opened.error().message());
  }
  return std::move(client);
}

auto test_request_message_wire_format() -> void {
  auto client = open_client();

  auto pending = client.call("sum", 1, 2);
  check_equal(client.transport_handle().outgoing.size(), std::size_t{1},
              "request must be sent");

  using request_type = std::tuple<std::uint8_t, std::uint32_t, std::string,
                                  std::tuple<int, int>>;
  const auto request =
      unpack_checked<request_type>(client.transport_handle().outgoing.front());

  check_equal(std::get<0>(request), msgpack::rpc::k_request_type,
              "request type must be 0");
  check_equal(std::get<1>(request), pending.msgid(), "msgid must round-trip");
  check_equal(std::get<2>(request), std::string{"sum"},
              "method name must be encoded");
  check_equal(std::get<3>(request), std::tuple{1, 2},
              "params must be encoded as array");
}

auto test_notification_message_wire_format() -> void {
  auto client = open_client();

  auto sent = client.notify("tick", 7, std::string{"ok"});
  if (!sent) {
    fail(sent.error().message());
  }

  check_equal(client.transport_handle().outgoing.size(), std::size_t{1},
              "notification must be sent");

  using notification_type =
      std::tuple<std::uint8_t, std::string, std::tuple<int, std::string>>;
  const auto notification = unpack_checked<notification_type>(
      client.transport_handle().outgoing.front());

  check_equal(std::get<0>(notification), msgpack::rpc::k_notification_type,
              "notification type must be 2");
  check_equal(std::get<1>(notification), std::string{"tick"},
              "notification method mismatch");
  check_equal(std::get<2>(notification), std::tuple{7, std::string{"ok"}},
              "notification params mismatch");
}

auto test_response_message_wire_format() -> void {
  const auto success = msgpack::rpc::detail::make_success_response_bytes(7, 42);
  if (!success) {
    fail(success.error().message());
  }
  const auto parsed = msgpack::rpc::detail::parse_response_id(*success);
  if (!parsed) {
    fail(parsed.error().message());
  }
  check_equal(*parsed, std::uint32_t{7}, "parse_response_id must succeed");

  using response_type =
      std::tuple<std::uint8_t, std::uint32_t, std::nullptr_t, int>;
  check_equal(unpack_checked<response_type>(*success),
              response_type{msgpack::rpc::k_response_type, 7, nullptr, 42},
              "response wire format mismatch");
}

auto test_parse_response_id_and_detail_builders() -> void {
  const auto success = msgpack::rpc::detail::make_success_response_bytes(9, 42);
  if (!success) {
    fail(success.error().message());
  }
  const auto parsed = msgpack::rpc::detail::parse_response_id(*success);
  if (!parsed) {
    fail(parsed.error().message());
  }
  check_equal(*parsed, std::uint32_t{9}, "success response id mismatch");

  using response_type =
      std::tuple<std::uint8_t, std::uint32_t, std::nullptr_t, int>;
  check_equal(unpack_checked<response_type>(*success),
              response_type{msgpack::rpc::k_response_type, 9, nullptr, 42},
              "success response format mismatch");

  const auto error =
      msgpack::rpc::detail::make_error_response_bytes(11, std::string{"boom"});
  if (!error) {
    fail(error.error().message());
  }
  using error_response_type =
      std::tuple<std::uint8_t, std::uint32_t, std::string, std::nullptr_t>;
  check_equal(unpack_checked<error_response_type>(*error),
              error_response_type{msgpack::rpc::k_response_type, 11,
                                  std::string{"boom"}, nullptr},
              "error response format mismatch");
}

auto test_out_of_order_responses() -> void {
  auto client = open_client();

  auto first = client.call("sum", 20, 22);
  auto second = client.call("sum", 7, 8);

  bool first_called = false;
  bool second_called = false;
  int first_value = 0;
  int second_value = 0;

  first.then([&](std::error_code error, int value) {
    check(!error, "first callback must not receive error");
    first_called = true;
    first_value = value;
  });
  second.then([&](std::error_code error, int value) {
    check(!error, "second callback must not receive error");
    second_called = true;
    second_value = value;
  });

  client.transport_handle().incoming.push_back(
      make_response(second.msgid(), nullptr, 15));
  client.transport_handle().incoming.push_back(
      make_response(first.msgid(), nullptr, 42));

  auto first_poll = client.poll();
  if (!first_poll) {
    fail(first_poll.error().message());
  }
  auto second_poll = client.poll();
  if (!second_poll) {
    fail(second_poll.error().message());
  }

  check(first_called, "first callback must run");
  check(second_called, "second callback must run");
  check_equal(first_value, 42, "first response must match msgid");
  check_equal(second_value, 15, "second response must match msgid");
}

auto test_remote_error_response() -> void {
  auto client = open_client();

  auto pending = client.call("fail");
  bool called = false;
  std::error_code received_error{};
  int received_value = 123;

  pending.then([&](std::error_code error, int value) {
    called = true;
    received_error = error;
    received_value = value;
  });

  client.transport_handle().incoming.push_back(
      make_response(pending.msgid(), std::string{"boom"}, nullptr));

  auto polled = client.poll();
  if (!polled) {
    fail(polled.error().message());
  }

  check(called, "error callback must run");
  check_equal(received_error,
              msgpack::rpc::make_error_code(msgpack::rpc::errc::remote_error),
              "remote error must map to remote_error");
  check_equal(received_value, 0, "default value must be provided on error");
}

auto test_request_dispatch_and_response() -> void {
  auto client = open_client();
  client.bind("mul", [](int a, int b) { return a * b; });

  client.transport_handle().incoming.push_back(make_request(100, "mul", 6, 7));
  auto polled = client.poll();
  if (!polled) {
    fail(polled.error().message());
  }

  check_equal(client.transport_handle().outgoing.size(), std::size_t{1},
              "request must produce response");

  using response_type =
      std::tuple<std::uint8_t, std::uint32_t, std::nullptr_t, int>;
  check_equal(
      unpack_checked<response_type>(client.transport_handle().outgoing.front()),
      response_type{msgpack::rpc::k_response_type, 100, nullptr, 42},
      "request response format mismatch");
}

auto test_notification_dispatch_without_response() -> void {
  auto client = open_client();

  bool called = false;
  int value = 0;
  client.bind("tick", [&](int input) {
    called = true;
    value = input;
  });

  client.transport_handle().incoming.push_back(make_notification("tick", 99));
  auto polled = client.poll();
  if (!polled) {
    fail(polled.error().message());
  }

  check(called, "notification handler must run");
  check_equal(value, 99, "notification arg mismatch");
  check_equal(client.transport_handle().outgoing.size(), std::size_t{0},
              "notification must not emit response");
}

auto test_unknown_method_request_behavior() -> void {
  auto client = open_client();
  client.transport_handle().incoming.push_back(make_request(77, "missing", 1));

  auto polled = client.poll();
  expect_error(
      polled, msgpack::rpc::make_error_code(msgpack::rpc::errc::unknown_method),
      "unknown request method must surface unknown_method");

  check_equal(client.transport_handle().outgoing.size(), std::size_t{1},
              "unknown request must still emit error response");
  using response_type =
      std::tuple<std::uint8_t, std::uint32_t, std::string, std::nullptr_t>;
  check_equal(
      unpack_checked<response_type>(client.transport_handle().outgoing.front()),
      response_type{msgpack::rpc::k_response_type, 77,
                    std::string{"method not found"}, nullptr},
      "unknown method response mismatch");
}

auto test_invalid_messages() -> void {
  expect_error(
      msgpack::rpc::detail::parse_inbound_message(
          make_bytes({0x92, 0x00, 0x01})),
      msgpack::rpc::make_error_code(msgpack::rpc::errc::invalid_message),
      "short request must be invalid");

  expect_error(
      msgpack::rpc::detail::parse_response_id(
          make_bytes({0x93, 0x01, 0x00, 0xc0})),
      msgpack::rpc::make_error_code(msgpack::rpc::errc::invalid_response),
      "response must have four elements");

  expect_error(
      msgpack::rpc::detail::parse_response_id(
          make_bytes({0x94, 0x02, 0x00, 0xc0, 0xc0})),
      msgpack::rpc::make_error_code(msgpack::rpc::errc::invalid_response_type),
      "response type must be 1");
}

auto test_invalid_response_delivery() -> void {
  auto client = open_client();
  auto pending = client.call("sum", 1, 2);

  bool called = false;
  std::error_code received{};
  pending.then([&](std::error_code error, int) {
    called = true;
    received = error;
  });

  client.transport_handle().incoming.push_back(
      make_bytes({0x93, 0x01, 0x00, 0xc0}));
  auto polled = client.poll();
  expect_error(
      polled,
      msgpack::rpc::make_error_code(msgpack::rpc::errc::invalid_response),
      "malformed response must fail poll before callback dispatch");
  check(!called,
        "callback must not run when response cannot be parsed as inbound "
        "message");

  client.transport_handle().incoming.clear();
  client.transport_handle().incoming.push_back(
      make_response(pending.msgid(), nullptr, std::tuple{1, 2}));
  auto next = client.poll();
  if (!next) {
    fail(next.error().message());
  }
  check(called, "callback must run on parseable response");
  check_equal(received, msgpack::make_error_code(msgpack::errc::type_mismatch),
              "tuple result must fail callback decoding");
}

auto test_unknown_response_id() -> void {
  auto client = open_client();
  client.transport_handle().incoming.push_back(make_response(999, nullptr, 7));

  auto polled = client.poll();
  expect_error(
      polled,
      msgpack::rpc::make_error_code(msgpack::rpc::errc::unknown_response_id),
      "unknown response id must be rejected");
}

struct test_case {
  std::string_view name;
  void (*fn)();
};

constexpr std::array k_tests{
    test_case{"request-wire", test_request_message_wire_format},
    test_case{"notification-wire", test_notification_message_wire_format},
    test_case{"response-wire", test_response_message_wire_format},
    test_case{"detail-builders", test_parse_response_id_and_detail_builders},
    test_case{"out-of-order", test_out_of_order_responses},
    test_case{"remote-error", test_remote_error_response},
    test_case{"request-dispatch", test_request_dispatch_and_response},
    test_case{"notification-dispatch",
              test_notification_dispatch_without_response},
    test_case{"unknown-method", test_unknown_method_request_behavior},
    test_case{"invalid-messages", test_invalid_messages},
    test_case{"invalid-response-delivery", test_invalid_response_delivery},
    test_case{"unknown-response-id", test_unknown_response_id},
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
