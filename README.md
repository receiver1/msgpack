# MessagePack
+ Completed in header-only format
+ Loaded with the C++23 standard
+ Full compliance with [specification](https://github.com/msgpack/msgpack/blob/master/spec.md)
+ Separated [MessagePack RPC spec](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md)
+ Charged with simple [qlibs/reflect](https://github.com/qlibs/reflect)
+ Compiled with `-fno-exceptions`

## MessagePack Specification Compliance
| Feature | Encode | Decode | Mapping |                                                     
|---|---|---|---|                                                                           
| `nil` | ✅ | ✅ | `std::nullptr_t`, `std::optional<T>` |                                  
| `bool` | ✅ | ✅ | `bool` |                                                               
| Positive fixint | ✅ | ✅ | `std::uint8_t` -> `std::uint64_t` |            
| Negative fixint | ✅ | ✅ | `std::int8_t` -> `std::int64_t` |                               
| `uint8/16/32/64` | ✅ | ✅ | `std::uint8_t` -> `std::uint64_t` |                                                                           
| `int8/16/32/64` | ✅ | ✅ | `std::int8_t` -> `std::int64_t` |                                                                                           
| `float32` | ✅ | ✅ | `float` |                                                           
| `float64` | ✅ | ✅ | `double` |                                                          
| `fixstr` | ✅ | ✅ | `std::string`, `std::string_view`, `const char*` |                   
| `str8` | ✅ | ✅ | `std::string`, `std::string_view`, `const char*` |                     
| `str16` | ✅ | ✅ | `std::string`, `std::string_view`, `const char*` |                    
| `str32` | ✅ | ✅ | `std::string`, `std::string_view`, `const char*` |                    
| `bin8` | ✅ | ✅ | `std::vector<std::byte>`, byte arrays, `std::span<const std::byte>` |  
| `bin16` | ✅ | ✅ | `std::vector<std::byte>`, byte arrays, `std::span<const std::byte>` | 
| `bin32` | ✅ | ✅ | `std::vector<std::byte>`, byte arrays, `std::span<const std::byte>` | 
| `fixarray` | ✅ | ✅ | `std::tuple`, `std::pair`, `std::array`, sequence containers |     
| `array16` | ✅ | ✅ | `std::tuple`, `std::pair`, `std::array`, sequence containers |      
| `array32` | ✅ | ✅ | `std::tuple`, `std::pair`, `std::array`, sequence containers |      
| `fixmap` | ✅ | ✅ | `std::map`, `std::unordered_map`, reflected structs |                
| `map16` | ✅ | ✅ | `std::map`, `std::unordered_map`, reflected structs |                 
| `map32` | ✅ | ✅ | `std::map`, `std::unordered_map`, reflected structs |                 
| `fixext1/2/4/8/16` | ✅ | ✅ | `msgpack::ext` |                                           
| `ext8` | ✅ | ✅ | `msgpack::ext` |                                                       
| `ext16` | ✅ | ✅ | `msgpack::ext` |                                                      
| `ext32` | ✅ | ✅ | `msgpack::ext` |                                                      
| Timestamp extension (`type = -1`) | ✅ | ✅ | `std::chrono::time_point` |
| Reserved marker `0xC1` | ❌ | ✅ | Invalid marker error on decode |      

## Installation
Just copy `msgpack.hpp` and `reflect.hpp` from `include/` folder to your project & it ready to use.  
You can also copy `msgpack_rpc.hpp` to use RPC interface. But need to implement your own transort.

## Examples
Examples can be found in the `examples/` directory.

Short RPC overview:
```cpp
// Transport independent
auto client_result = msgpack::rpc
  ::open(scripted_transport{}, "loopback://demo");

// Exceptions-free
if (!client_result.has_value()) {
  return;
}
auto client = std::move(*client_result);

// Event-driven
client.call<"sum">(5, 10)
  .then([](std::error_code error, int result) -> void {
    // All errors in one place
    if (error) {
      std::println("error: {:s}", error.message());
      return;
    }

    std::println("sum: {:d}", result);
  });
client.notify<"ping">();

// Full-duplex
client.bind<"sum">([](int a, int b) -> std::expected<int, std::string> {
  if (a > 100) {
    // Throw error simple
    return std::unexpected("a too large");
  }

  return a + b;
});
client.bind<"ping">([]() -> void {
  std::println("ping notify");
});

// Thread-safe
std::thread([]() {
  // Execution flow
  while (1) {
    client.poll();
    // <other stuff>
  }
});
```
