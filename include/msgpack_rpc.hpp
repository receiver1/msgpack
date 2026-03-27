#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "msgpack.hpp"

namespace msgpack::rpc {

template <std::size_t N>
struct fixed_string {
  constexpr fixed_string(const char (&value)[N]) {
    for (std::size_t index = 0; index != N; ++index) {
      data[index] = value[index];
    }
  }

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return {data, N - 1};
  }

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return N - 1;
  }

  char data[N]{};
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

enum class errc {
  ok = 0,
  invalid_message,
  invalid_response,
  invalid_response_type,
  response_result_required,
  response_result_must_be_nil,
  duplicate_response,
  unknown_response_id,
  unknown_method,
  not_connected,
  request_id_exhausted,
  remote_error,
  dangling_call_handle,
};

class error_category final : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override {
    return "msgpack-rpc";
  }

  [[nodiscard]] auto message(int condition) const -> std::string override {
    switch (static_cast<errc>(condition)) {
      case errc::ok:
        return "ok";
      case errc::invalid_message:
        return "invalid message";
      case errc::invalid_response:
        return "invalid response";
      case errc::invalid_response_type:
        return "invalid response type";
      case errc::response_result_required:
        return "response result is required";
      case errc::response_result_must_be_nil:
        return "response result must be nil";
      case errc::duplicate_response:
        return "duplicate response";
      case errc::unknown_response_id:
        return "unknown response id";
      case errc::unknown_method:
        return "unknown method";
      case errc::not_connected:
        return "not connected";
      case errc::request_id_exhausted:
        return "request id space exhausted";
      case errc::remote_error:
        return "remote error";
      case errc::dangling_call_handle:
        return "dangling call handle";
    }

    return "unknown msgpack-rpc error";
  }
};

inline const error_category k_error_category{};

[[nodiscard]] inline auto make_error_code(errc code) -> std::error_code {
  return {static_cast<int>(code), k_error_category};
}

inline constexpr std::uint8_t k_request_type = 0;
inline constexpr std::uint8_t k_response_type = 1;
inline constexpr std::uint8_t k_notification_type = 2;

template <typename Transport>
concept transport = requires(Transport& value,
                             const typename Transport::endpoint_type& endpoint,
                             std::span<const std::byte> bytes) {
  typename Transport::endpoint_type;
  {
    value.connect(endpoint)
  } -> std::same_as<std::expected<void, std::error_code>>;
  { value.close() } -> std::same_as<std::expected<void, std::error_code>>;
  { value.send(bytes) } -> std::same_as<std::expected<void, std::error_code>>;
  {
    value.receive()
  } -> std::same_as<
      std::expected<std::optional<std::vector<std::byte>>, std::error_code>>;
};

namespace detail {

[[nodiscard]] inline auto invalid_response()
    -> std::unexpected<std::error_code> {
  return std::unexpected(make_error_code(errc::invalid_response));
}

inline auto skip_objects(msgpack::reader& in, std::size_t count)
    -> std::expected<void, std::error_code> {
  for (std::size_t index = 0; index != count; ++index) {
    auto status = in.skip();
    if (!status) {
      return std::unexpected(status.error());
    }
  }
  return {};
}

inline auto require_eof(msgpack::reader& in, errc code)
    -> std::expected<void, std::error_code> {
  if (in.remaining() != 0) {
    return std::unexpected(make_error_code(code));
  }
  return {};
}

[[nodiscard]] inline auto read_response_msgid(msgpack::reader& in)
    -> std::expected<std::uint32_t, std::error_code> {
  auto size = in.read_array_header();
  if (!size) {
    return std::unexpected(size.error());
  }
  if (*size != 4) {
    return invalid_response();
  }

  auto type = in.read_integer<std::uint8_t>();
  if (!type) {
    return std::unexpected(type.error());
  }
  if (*type != k_response_type) {
    return std::unexpected(make_error_code(errc::invalid_response_type));
  }

  auto msgid = in.read_integer<std::uint32_t>();
  if (!msgid) {
    return std::unexpected(msgid.error());
  }

  return *msgid;
}

struct method_params {
  std::string method{};
  std::span<const std::byte> params{};
};

[[nodiscard]] inline auto read_method_params(msgpack::reader& in,
                                             std::span<const std::byte> bytes,
                                             errc code)
    -> std::expected<method_params, std::error_code> {
  auto method = in.read_string();
  if (!method) {
    return std::unexpected(method.error());
  }

  const auto params_offset = in.offset();
  auto status = in.skip();
  if (!status) {
    return std::unexpected(status.error());
  }
  status = require_eof(in, code);
  if (!status) {
    return std::unexpected(status.error());
  }

  return method_params{std::move(*method), bytes.subspan(params_offset)};
}

[[nodiscard]] inline auto parse_response_id(std::span<const std::byte> bytes)
    -> std::expected<std::uint32_t, std::error_code> {
  msgpack::reader in{bytes};

  auto msgid = read_response_msgid(in);
  if (!msgid) {
    return std::unexpected(msgid.error());
  }

  auto status = skip_objects(in, 2);
  if (!status) {
    return std::unexpected(status.error());
  }
  status = require_eof(in, errc::invalid_response);
  if (!status) {
    return std::unexpected(status.error());
  }

  return *msgid;
}

inline auto consume_nil(msgpack::reader& in)
    -> std::expected<void, std::error_code> {
  auto nil = msgpack::unpack<std::nullptr_t>(in);
  if (!nil) {
    return std::unexpected(
        nil.error() == msgpack::make_error_code(msgpack::errc::type_mismatch)
            ? make_error_code(errc::response_result_must_be_nil)
            : nil.error());
  }
  return {};
}

template <typename>
struct callable_traits;

template <typename Return, typename... Args>
struct callable_traits<Return(Args...)> {
  using args_tuple = std::tuple<std::remove_cvref_t<Args>...>;
  using return_type = Return;
};

template <typename Return, typename... Args>
struct callable_traits<Return (*)(Args...)> : callable_traits<Return(Args...)> {
};

template <typename Class, typename Return, typename... Args>
struct callable_traits<Return (Class::*)(Args...)>
    : callable_traits<Return(Args...)> {};

template <typename Class, typename Return, typename... Args>
struct callable_traits<Return (Class::*)(Args...) const>
    : callable_traits<Return(Args...)> {};

template <typename Callable, typename = void>
struct callable_signature {
  using type = decltype(&std::remove_cvref_t<Callable>::operator());
};

template <typename Return, typename... Args>
struct callable_signature<Return (*)(Args...), void> {
  using type = Return(Args...);
};

template <typename Return, typename... Args>
struct callable_signature<Return(Args...), void> {
  using type = Return(Args...);
};

template <typename Callable>
using callable_signature_t =
    typename callable_signature<std::remove_cvref_t<Callable>>::type;

template <typename Tuple>
struct tuple_tail;

template <typename Head, typename... Tail>
struct tuple_tail<std::tuple<Head, Tail...>> {
  using type = std::tuple<Tail...>;
};

template <typename Callback>
using callback_args_tuple_t =
    typename callable_traits<callable_signature_t<Callback>>::args_tuple;

template <typename Callback>
using callback_result_tuple_t =
    typename tuple_tail<callback_args_tuple_t<Callback>>::type;

template <typename Handler>
using handler_args_tuple_t =
    typename callable_traits<callable_signature_t<Handler>>::args_tuple;

template <typename Handler>
using handler_return_t =
    typename callable_traits<callable_signature_t<Handler>>::return_type;

template <typename Callback>
inline constexpr bool callback_invocable_v =
    (std::tuple_size_v<callback_args_tuple_t<Callback>> >= 1) &&
    std::same_as<std::tuple_element_t<0, callback_args_tuple_t<Callback>>,
                 std::error_code>;

template <typename T>
struct is_expected : std::false_type {};

template <typename T, typename E>
struct is_expected<std::expected<T, E>> : std::true_type {
  using value_type = T;
  using error_type = E;
};

template <typename T>
inline constexpr bool is_expected_v =
    is_expected<std::remove_cvref_t<T>>::value;

struct inbound_message {
  std::uint8_t type{};
  std::optional<std::uint32_t> msgid{};
  std::string method{};
  std::span<const std::byte> params{};
};

template <typename Callback, typename Tuple>
inline auto invoke_callback(Callback& callback, std::error_code error,
                            Tuple&& values) -> void {
  std::apply(
      [&](auto&&... unpacked) {
        std::invoke(callback, error,
                    std::forward<decltype(unpacked)>(unpacked)...);
      },
      std::forward<Tuple>(values));
}

template <typename Callback>
inline auto invoke_callback_error(Callback& callback, std::error_code error)
    -> void {
  using result_tuple = callback_result_tuple_t<Callback>;

  if constexpr (std::invocable<Callback&, std::error_code>) {
    std::invoke(callback, error);
  } else if constexpr (std::default_initializable<result_tuple>) {
    invoke_callback(callback, error, result_tuple{});
  } else {
    static_assert(std::invocable<Callback&, std::error_code>,
                  "Callback must accept std::error_code or have "
                  "default-constructible result arguments");
  }
}

template <typename Callback, typename Tuple>
inline auto invoke_callback_success_tuple(Callback& callback, Tuple&& values)
    -> void {
  invoke_callback(callback, std::error_code{}, std::forward<Tuple>(values));
}

template <typename Tuple>
[[nodiscard]] inline auto decode_result_tuple(msgpack::reader& in)
    -> std::expected<Tuple, std::error_code> {
  if constexpr (std::tuple_size_v<Tuple> == 0) {
    auto status = consume_nil(in);
    if (!status) {
      return std::unexpected(status.error());
    }
    return Tuple{};
  } else if constexpr (std::tuple_size_v<Tuple> == 1) {
    using value_type = std::tuple_element_t<0, Tuple>;
    auto value = msgpack::unpack<value_type>(in);
    if (!value) {
      return std::unexpected(value.error());
    }
    return Tuple{std::move(*value)};
  } else {
    auto values = msgpack::unpack<Tuple>(in);
    if (!values) {
      return std::unexpected(values.error());
    }
    return std::move(*values);
  }
}

[[nodiscard]] inline auto parse_inbound_message(
    std::span<const std::byte> bytes)
    -> std::expected<inbound_message, std::error_code> {
  msgpack::reader in{bytes};
  inbound_message message{};

  auto size = in.read_array_header();
  if (!size) {
    return std::unexpected(size.error());
  }

  auto type = in.read_integer<std::uint8_t>();
  if (!type) {
    return std::unexpected(type.error());
  }
  message.type = *type;

  switch (message.type) {
    case k_request_type: {
      if (*size != 4) {
        return std::unexpected(make_error_code(errc::invalid_message));
      }

      auto msgid = in.read_integer<std::uint32_t>();
      if (!msgid) {
        return std::unexpected(msgid.error());
      }
      auto params = read_method_params(in, bytes, errc::invalid_message);
      if (!params) {
        return std::unexpected(params.error());
      }

      message.msgid = *msgid;
      message.method = std::move(params->method);
      message.params = params->params;
      return message;
    }

    case k_response_type: {
      if (*size != 4) {
        return std::unexpected(make_error_code(errc::invalid_response));
      }

      auto msgid = in.read_integer<std::uint32_t>();
      if (!msgid) {
        return std::unexpected(msgid.error());
      }

      auto status = skip_objects(in, 2);
      if (!status) {
        return std::unexpected(status.error());
      }
      status = require_eof(in, errc::invalid_response);
      if (!status) {
        return std::unexpected(status.error());
      }

      message.msgid = *msgid;
      return message;
    }

    case k_notification_type: {
      if (*size != 3) {
        return std::unexpected(make_error_code(errc::invalid_message));
      }

      auto params = read_method_params(in, bytes, errc::invalid_message);
      if (!params) {
        return std::unexpected(params.error());
      }

      message.method = std::move(params->method);
      message.params = params->params;
      return message;
    }
  }

  return std::unexpected(make_error_code(errc::invalid_message));
}

[[nodiscard]] inline auto make_success_response_bytes(std::uint32_t msgid)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  return msgpack::pack(std::tuple{k_response_type, msgid, nullptr, nullptr});
}

template <typename Result>
[[nodiscard]] inline auto make_success_response_bytes(std::uint32_t msgid,
                                                      Result&& result)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  return msgpack::pack(std::tuple{k_response_type, msgid, nullptr,
                                  std::forward<Result>(result)});
}

template <typename Error>
[[nodiscard]] inline auto make_error_response_bytes(std::uint32_t msgid,
                                                    Error&& error)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  return msgpack::pack(
      std::tuple{k_response_type, msgid, std::forward<Error>(error), nullptr});
}

template <typename Handler, typename Tuple>
inline decltype(auto) invoke_handler(Handler& handler, Tuple&& values) {
  return std::apply(
      [&](auto&&... unpacked) -> decltype(auto) {
        return std::invoke(handler,
                           std::forward<decltype(unpacked)>(unpacked)...);
      },
      std::forward<Tuple>(values));
}

template <typename Handler>
inline auto dispatch_notification(Handler& handler,
                                  std::span<const std::byte> params)
    -> std::expected<void, std::error_code> {
  using args_tuple = handler_args_tuple_t<Handler>;

  auto args = msgpack::unpack<args_tuple>(params);
  if (!args) {
    return std::unexpected(args.error());
  }

  if constexpr (std::is_void_v<handler_return_t<Handler>>) {
    invoke_handler(handler, *args);
  } else {
    auto ignored = invoke_handler(handler, *args);
    (void)ignored;
  }

  return {};
}

template <typename Handler>
inline auto dispatch_request(std::uint32_t msgid, Handler& handler,
                             std::span<const std::byte> params)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  using args_tuple = handler_args_tuple_t<Handler>;
  using return_type = handler_return_t<Handler>;

  auto args = msgpack::unpack<args_tuple>(params);
  if (!args) {
    return make_error_response_bytes(msgid,
                                     std::string{args.error().message()});
  }

  if constexpr (is_expected_v<return_type>) {
    using expected_type = std::remove_cvref_t<return_type>;
    using value_type = typename is_expected<expected_type>::value_type;

    auto result = invoke_handler(handler, *args);
    if (!result) {
      return make_error_response_bytes(msgid, result.error());
    }

    if constexpr (std::is_void_v<value_type>) {
      return make_success_response_bytes(msgid);
    } else {
      return make_success_response_bytes(msgid, *result);
    }
  } else if constexpr (std::is_void_v<return_type>) {
    invoke_handler(handler, *args);
    return make_success_response_bytes(msgid);
  } else {
    auto result = invoke_handler(handler, *args);
    return make_success_response_bytes(msgid, std::move(result));
  }
}

struct bound_handler {
  std::move_only_function<
      std::expected<std::optional<std::vector<std::byte>>, std::error_code>(
          std::optional<std::uint32_t>, std::span<const std::byte>)>
      invoke{};
};

template <typename Handler>
[[nodiscard]] inline auto make_bound_handler(Handler&& handler)
    -> std::shared_ptr<bound_handler> {
  auto handler_ptr =
      std::make_shared<std::decay_t<Handler>>(std::forward<Handler>(handler));
  auto bound = std::make_shared<bound_handler>();
  bound->invoke = [handler = std::move(handler_ptr)](
                      std::optional<std::uint32_t> msgid,
                      std::span<const std::byte> params)
      -> std::expected<std::optional<std::vector<std::byte>>, std::error_code> {
    if (msgid) {
      auto response = dispatch_request(*msgid, *handler, params);
      if (!response) {
        return std::unexpected(response.error());
      }
      return std::optional<std::vector<std::byte>>{std::move(*response)};
    }

    auto status = dispatch_notification(*handler, params);
    if (!status) {
      return std::unexpected(status.error());
    }
    return std::optional<std::vector<std::byte>>{};
  };
  return bound;
}

template <typename Callback>
inline auto invoke_response_callback(Callback& callback,
                                     std::span<const std::byte> bytes) -> void {
  using result_tuple = callback_result_tuple_t<Callback>;
  msgpack::reader in{bytes};

  if (auto msgid = read_response_msgid(in); !msgid) {
    invoke_callback_error(callback, msgid.error());
    return;
  }

  auto marker = in.peek();
  if (!marker) {
    invoke_callback_error(callback, marker.error());
    return;
  }
  if (*marker != std::byte{0xc0}) {
    auto status = in.skip();
    if (!status) {
      invoke_callback_error(callback, status.error());
      return;
    }
    auto nil = consume_nil(in);
    if (!nil) {
      invoke_callback_error(callback, nil.error());
      return;
    }
    status = require_eof(in, errc::invalid_response);
    if (!status) {
      invoke_callback_error(callback, status.error());
      return;
    }
    invoke_callback_error(callback, make_error_code(errc::remote_error));
    return;
  }

  auto error_nil = consume_nil(in);
  if (!error_nil) {
    invoke_callback_error(callback, error_nil.error());
    return;
  }

  auto result = decode_result_tuple<result_tuple>(in);
  if (!result) {
    invoke_callback_error(callback, result.error());
    return;
  }
  auto status = require_eof(in, errc::invalid_response);
  if (!status) {
    invoke_callback_error(callback, status.error());
    return;
  }

  invoke_callback_success_tuple(callback, std::move(*result));
}

template <typename... Args>
[[nodiscard]] inline auto make_request_bytes(std::uint32_t msgid,
                                             std::string_view method,
                                             Args&&... args)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  return msgpack::pack(std::tuple{
      k_request_type, msgid, std::string{method},
      std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}});
}

template <typename... Args>
[[nodiscard]] inline auto make_notification_bytes(std::string_view method,
                                                  Args&&... args)
    -> std::expected<std::vector<std::byte>, std::error_code> {
  return msgpack::pack(std::tuple{
      k_notification_type, std::string{method},
      std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}});
}

struct pending_state {
  mutable std::mutex mutex{};
  std::optional<std::error_code> local_error{};
  std::shared_ptr<const std::vector<std::byte>> response{};
  std::move_only_function<void()> callback{};

  [[nodiscard]] auto ready() const noexcept -> bool {
    std::lock_guard lock{mutex};
    return local_error.has_value() || static_cast<bool>(response);
  }
};

inline auto resolve_pending_error(const std::shared_ptr<pending_state>& state,
                                  std::error_code error) -> void {
  std::move_only_function<void()> callback{};
  {
    std::lock_guard lock{state->mutex};
    state->local_error = error;
    if (state->callback) {
      callback = std::move(state->callback);
      state->callback = {};
    }
  }
  if (callback) {
    callback();
  }
}

inline auto resolve_pending_response(
    const std::shared_ptr<pending_state>& state,
    std::vector<std::byte> response) -> void {
  std::move_only_function<void()> callback{};
  {
    std::lock_guard lock{state->mutex};
    state->response =
        std::make_shared<const std::vector<std::byte>>(std::move(response));
    if (state->callback) {
      callback = std::move(state->callback);
      state->callback = {};
    }
  }
  if (callback) {
    callback();
  }
}

template <typename Callback>
inline auto attach_then(const std::shared_ptr<pending_state>& state,
                        Callback&& callback) -> void {
  auto callback_ptr = std::make_shared<std::decay_t<Callback>>(
      std::forward<Callback>(callback));
  std::optional<std::error_code> local_error{};
  std::shared_ptr<const std::vector<std::byte>> response{};

  {
    std::lock_guard lock{state->mutex};
    if (state->local_error) {
      local_error = *state->local_error;
    } else if (state->response) {
      response = state->response;
    } else {
      state->callback = [state, callback = std::move(callback_ptr)]() mutable {
        std::optional<std::error_code> ready_error{};
        std::shared_ptr<const std::vector<std::byte>> ready_response{};

        {
          std::lock_guard lock{state->mutex};
          if (state->local_error) {
            ready_error = *state->local_error;
          } else {
            ready_response = state->response;
          }
        }

        if (ready_error) {
          invoke_callback_error(*callback, *ready_error);
          return;
        }
        if (ready_response) {
          invoke_response_callback(*callback, *ready_response);
        }
      };
      return;
    }
  }

  if (local_error) {
    invoke_callback_error(*callback_ptr, *local_error);
    return;
  }
  if (response) {
    invoke_response_callback(*callback_ptr, *response);
  }
}

}  // namespace detail

template <typename Transport>
  requires transport<Transport>
class basic_client;

template <typename Transport>
  requires transport<Transport>
class pending_call {
 public:
  pending_call() = default;

  [[nodiscard]] auto msgid() const noexcept -> std::uint32_t { return msgid_; }
  [[nodiscard]] auto ready() const noexcept -> bool {
    return state_ != nullptr && state_->ready();
  }

  template <typename Callback>
    requires(detail::callback_invocable_v<std::decay_t<Callback>>)
  auto then(Callback&& callback) -> pending_call& {
    if (state_ == nullptr) {
      return *this;
    }

    detail::attach_then(state_, std::forward<Callback>(callback));
    return *this;
  }

 private:
  friend class basic_client<Transport>;

  pending_call(std::uint32_t msgid,
               std::shared_ptr<detail::pending_state> state)
      : msgid_(msgid), state_(std::move(state)) {}

  std::uint32_t msgid_{0};
  std::shared_ptr<detail::pending_state> state_{};
};

template <typename Transport>
  requires transport<Transport>
class basic_client {
 public:
  using transport_type = Transport;
  using endpoint_type = typename Transport::endpoint_type;

  basic_client()
    requires std::default_initializable<Transport>
  = default;
  basic_client(const basic_client&) = delete;
  auto operator=(const basic_client&) -> basic_client& = delete;
  basic_client(basic_client&& other) noexcept(
      std::is_nothrow_move_constructible_v<Transport>)
      : transport_(std::move(other.transport_)),
        connected_(other.connected_),
        next_msgid_(other.next_msgid_),
        pending_(std::move(other.pending_)),
        handlers_(std::move(other.handlers_)) {
    other.connected_ = false;
    other.next_msgid_ = 0;
  }
  auto operator=(basic_client&& other) noexcept(
      std::is_nothrow_move_assignable_v<Transport>) -> basic_client& {
    if (this == &other) {
      return *this;
    }

    std::scoped_lock lock{mutex_, other.mutex_};
    transport_ = std::move(other.transport_);
    connected_ = other.connected_;
    next_msgid_ = other.next_msgid_;
    pending_ = std::move(other.pending_);
    handlers_ = std::move(other.handlers_);
    other.connected_ = false;
    other.next_msgid_ = 0;
    return *this;
  }

  explicit basic_client(Transport transport)
      : transport_(std::move(transport)) {}

  auto connect(const endpoint_type& endpoint)
      -> std::expected<void, std::error_code> {
    auto reset = close();
    if (!reset) {
      return std::unexpected(reset.error());
    }

    auto status = transport_.connect(endpoint);
    if (!status) {
      return std::unexpected(status.error());
    }

    {
      std::lock_guard lock{mutex_};
      connected_ = true;
    }
    return {};
  }

  auto close(
      std::error_code reason = make_error_code(errc::dangling_call_handle))
      -> std::expected<void, std::error_code> {
    std::vector<std::shared_ptr<detail::pending_state>> pending_states{};
    bool was_connected = false;

    {
      std::lock_guard lock{mutex_};
      was_connected = connected_;
      pending_states.reserve(pending_.size());
      for (auto& [msgid, entry] : pending_) {
        (void)msgid;
        pending_states.push_back(std::move(entry.state));
      }
      pending_.clear();
      next_msgid_ = 0;
      connected_ = false;
    }

    for (const auto& state : pending_states) {
      detail::resolve_pending_error(state, reason);
    }

    if (!was_connected) {
      return {};
    }

    auto status = transport_.close();
    if (!status) {
      return std::unexpected(status.error());
    }

    return {};
  }

  [[nodiscard]] auto transport_handle() noexcept -> Transport& {
    return transport_;
  }

  [[nodiscard]] auto transport_handle() const noexcept -> const Transport& {
    return transport_;
  }

  [[nodiscard]] auto outstanding() const noexcept -> std::size_t {
    std::lock_guard lock{mutex_};
    return pending_.size();
  }

  template <fixed_string Method, typename Handler>
  auto bind(Handler&& handler) -> void {
    bind(std::string_view{Method}, std::forward<Handler>(handler));
  }

  template <typename Handler>
  auto bind(std::string_view method, Handler&& handler) -> void {
    auto bound = detail::make_bound_handler(std::forward<Handler>(handler));
    std::lock_guard lock{mutex_};
    handlers_[std::string{method}] = std::move(bound);
  }

  template <fixed_string Method, typename... Args>
  auto call(Args&&... args) -> pending_call<Transport> {
    return call(std::string_view{Method}, std::forward<Args>(args)...);
  }

  template <typename... Args>
  auto call(std::string_view method, Args&&... args)
      -> pending_call<Transport> {
    auto state = std::make_shared<detail::pending_state>();
    std::uint32_t msgid = 0;
    {
      std::lock_guard lock{mutex_};
      if (!connected_) {
        detail::resolve_pending_error(state,
                                      make_error_code(errc::not_connected));
        return pending_call<Transport>{0, std::move(state)};
      }

      auto allocated = allocate_msgid();
      if (!allocated) {
        detail::resolve_pending_error(state, allocated.error());
        return pending_call<Transport>{0, std::move(state)};
      }
      msgid = *allocated;
    }

    auto bytes =
        detail::make_request_bytes(msgid, method, std::forward<Args>(args)...);
    if (!bytes) {
      detail::resolve_pending_error(state, bytes.error());
      return pending_call<Transport>{msgid, std::move(state)};
    }

    {
      std::lock_guard lock{mutex_};
      pending_.emplace(msgid, pending_entry{state});

      auto status = transport_.send(*bytes);
      if (!status) {
        pending_.erase(msgid);
        detail::resolve_pending_error(state, status.error());
        return pending_call<Transport>{msgid, std::move(state)};
      }
    }

    return pending_call<Transport>{msgid, std::move(state)};
  }

  template <typename... Args>
  auto notify(std::string_view method, Args&&... args)
      -> std::expected<void, std::error_code> {
    auto bytes =
        detail::make_notification_bytes(method, std::forward<Args>(args)...);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    std::lock_guard lock{mutex_};
    if (!connected_) {
      return std::unexpected(make_error_code(errc::not_connected));
    }
    return transport_.send(*bytes);
  }

  template <fixed_string Method, typename... Args>
  auto notify(Args&&... args) -> std::expected<void, std::error_code> {
    return notify(std::string_view{Method}, std::forward<Args>(args)...);
  }

  auto poll() -> std::expected<bool, std::error_code> {
    std::vector<std::byte> bytes{};

    {
      std::lock_guard lock{mutex_};
      if (!connected_) {
        return std::unexpected(make_error_code(errc::not_connected));
      }

      auto received = transport_.receive();
      if (!received) {
        return std::unexpected(received.error());
      }
      if (!received->has_value()) {
        return false;
      }
      bytes = std::move(**received);
    }

    auto message = detail::parse_inbound_message(bytes);
    if (!message) {
      return std::unexpected(message.error());
    }

    if (message->type == k_response_type) {
      std::shared_ptr<detail::pending_state> state{};
      {
        std::lock_guard lock{mutex_};
        auto it = pending_.find(*message->msgid);
        if (it == pending_.end()) {
          return std::unexpected(make_error_code(errc::unknown_response_id));
        }

        state = std::move(it->second.state);
        pending_.erase(it);
      }

      detail::resolve_pending_response(state, std::move(bytes));
      return true;
    }

    std::shared_ptr<detail::bound_handler> handler{};
    {
      std::lock_guard lock{mutex_};
      auto it = handlers_.find(message->method);
      if (it != handlers_.end()) {
        handler = it->second;
      }
    }

    if (!handler) {
      if (message->type == k_request_type) {
        auto response = detail::make_error_response_bytes(
            *message->msgid, std::string{"method not found"});
        if (!response) {
          return std::unexpected(response.error());
        }

        std::lock_guard lock{mutex_};
        auto sent = transport_.send(*response);
        if (!sent) {
          return std::unexpected(sent.error());
        }
      }

      return std::unexpected(make_error_code(errc::unknown_method));
    }

    auto response = handler->invoke(message->msgid, message->params);
    if (!response) {
      if (message->type == k_request_type) {
        auto error_response = detail::make_error_response_bytes(
            *message->msgid, std::string{response.error().message()});
        if (!error_response) {
          return std::unexpected(error_response.error());
        }

        std::lock_guard lock{mutex_};
        auto sent = transport_.send(*error_response);
        if (!sent) {
          return std::unexpected(sent.error());
        }
        return true;
      }

      return std::unexpected(response.error());
    }

    if (*response) {
      std::lock_guard lock{mutex_};
      auto sent = transport_.send(**response);
      if (!sent) {
        return std::unexpected(sent.error());
      }
    }

    return true;
  }

 private:
  struct pending_entry {
    std::shared_ptr<detail::pending_state> state{};
  };

  [[nodiscard]] auto allocate_msgid()
      -> std::expected<std::uint32_t, std::error_code> {
    const auto start = next_msgid_;

    do {
      const auto candidate = next_msgid_;
      ++next_msgid_;

      if (!pending_.contains(candidate)) {
        return candidate;
      }
    } while (next_msgid_ != start);

    return std::unexpected(make_error_code(errc::request_id_exhausted));
  }

  Transport transport_;
  mutable std::mutex mutex_{};
  bool connected_{false};
  std::uint32_t next_msgid_{0};
  std::unordered_map<std::uint32_t, pending_entry> pending_{};
  std::unordered_map<std::string, std::shared_ptr<detail::bound_handler>>
      handlers_{};
};

template <typename Transport>
using client = basic_client<Transport>;
}  // namespace msgpack::rpc

namespace std {

template <>
struct is_error_code_enum<msgpack::rpc::errc> : true_type {};

}  // namespace std
