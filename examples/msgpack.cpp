#include "msgpack.hpp"

#include <cstdlib>
#include <system_error>

void hexdump(void *ptr, int buflen) {
  unsigned char *buf = (unsigned char *)ptr;
  int i, j;
  for (i = 0; i < buflen; i += 16) {
    printf("%06x: ", i);
    for (j = 0; j < 16; j++)
      if (i + j < buflen)
        printf("%02x ", buf[i + j]);
      else
        printf("   ");
    printf(" ");
    for (j = 0; j < 16; j++)
      if (i + j < buflen) printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
    printf("\n");
  }
}

struct user_t {
  std::uint64_t id;
  std::string name;
  std::string email;
  std::string password;
  std::uint8_t age;
};

int main(int argc, char *argv[]) {
  user_t input{
      .id = 1,
      .name = "John Doe",
      .email = "john.doe@example.com",
      .password = "s3cr3t",
      .age = 30,
  };

  printf("input.email: %s\n", input.email.c_str());

  auto result = msgpack::pack(input).and_then([](auto bytes) {
    hexdump(bytes.data(), bytes.size());
    return msgpack::unpack<user_t>(bytes);
  });
  if (!result) {
    printf("msgpack error: %s\n", result.error().message().c_str());
    return EXIT_FAILURE;
  }

  printf("output.email: %s\n", result->email.c_str());
  return EXIT_SUCCESS;
}