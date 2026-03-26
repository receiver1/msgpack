#include "msgpack_rpc.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

void hexdump(const void* ptr, int buflen) {
  const auto* buf = static_cast<const unsigned char*>(ptr);
  for (int i = 0; i < buflen; i += 16) {
    printf("%06x: ", i);
    for (int j = 0; j < 16; ++j) {
      if (i + j < buflen) {
        printf("%02x ", buf[i + j]);
      } else {
        printf("   ");
      }
    }
    printf(" ");
    for (int j = 0; j < 16; ++j) {
      if (i + j < buflen) {
        printf("%c", std::isprint(buf[i + j]) ? buf[i + j] : '.');
      }
    }
    printf("\n");
  }
}

struct scripted_transport {
  using endpoint_type = std::string;

  std::string endpoint{};
  std::deque<std::vector<std::byte>> incoming{};
  std::vector<std::vector<std::byte>> outgoing{};

  auto connect(const endpoint_type& endpoint)
      -> std::expected<void, std::error_code> {
    this->endpoint = endpoint;
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

auto enqueue_sum_response(scripted_transport& transport, std::uint32_t msgid,
                          int value) -> std::expected<void, std::error_code> {
  auto bytes = msgpack::pack(std::tuple{msgpack::rpc::k_response_type, msgid,
                                        std::optional<std::string>{},
                                        std::optional<int>{value}});
  if (!bytes) {
    return std::unexpected(bytes.error());
  }

  transport.incoming.push_back(std::move(*bytes));
  return {};
}

int main() {
  auto client_result = msgpack::rpc::client<scripted_transport>::open(
      scripted_transport{}, "loopback://demo");
  if (!client_result) {
    printf("rpc transport error: %s\n",
           client_result.error().message().c_str());
    return EXIT_FAILURE;
  }

  auto client = std::move(*client_result);

  bool first_called = false;
  bool second_called = false;
  int first_value = 0;
  int second_value = 0;

  auto first = client.call<"sum">(20, 22);
  auto second = client.call<"sum">(7, 8);

  first.then([&](std::error_code error, int value) {
    if (error) {
      printf("rpc callback error: %s\n", error.message().c_str());
      return;
    }

    first_called = true;
    first_value = value;
  });

  second.then([&](std::error_code error, int value) {
    if (error) {
      printf("rpc callback error: %s\n", error.message().c_str());
      return;
    }

    second_called = true;
    second_value = value;
  });

  auto first_response =
      enqueue_sum_response(client.transport_handle(), first.msgid(), 42);
  if (!first_response) {
    printf("rpc preload error: %s\n", first_response.error().message().c_str());
    return EXIT_FAILURE;
  }

  auto second_response =
      enqueue_sum_response(client.transport_handle(), second.msgid(), 15);
  if (!second_response) {
    printf("rpc preload error: %s\n",
           second_response.error().message().c_str());
    return EXIT_FAILURE;
  }

  auto notify = client.notify<"ping">();
  if (!notify) {
    printf("rpc notify error: %s\n", notify.error().message().c_str());
    return EXIT_FAILURE;
  }

  for (int step = 0; step < 2; ++step) {
    auto polled = client.poll();
    if (!polled) {
      printf("rpc poll error: %s\n", polled.error().message().c_str());
      return EXIT_FAILURE;
    }
  }

  if (!first_called || !second_called) {
    printf("rpc callback was not called\n");
    return EXIT_FAILURE;
  }

  printf("sum(20, 22) = %d\n", first_value);
  printf("sum(7, 8) = %d\n", second_value);
  printf("connected to: %s\n", client.transport_handle().endpoint.c_str());
  printf("sent packets: %zu\n", client.transport_handle().outgoing.size());

  for (std::size_t index = 0; index < client.transport_handle().outgoing.size();
       ++index) {
    const auto& packet = client.transport_handle().outgoing[index];
    printf("packet[%zu]\n", index);
    hexdump(packet.data(), static_cast<int>(packet.size()));
  }

  return EXIT_SUCCESS;
}
