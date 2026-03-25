## MessagePack
+ Completed in header-only format
+ Loaded with the C++23 standard
+ Full compliance with specification
+ Separated MessagePack RPC spec
+ Charged with simple [qlibs/reflect](https://github.com/qlibs/reflect)

### MessagePack Specification Compliance
| Feature | Encode | Decode | Mapping |                                                     
|---|---|---|---|                                                                           
| `nil` | ✅ | ✅ | `std::nullptr_t`, `std::optional<T>` |                                  
| `bool` | ✅ | ✅ | `bool` |                                                               
| Positive fixint | ✅ | ✅ | `std::uint8_t` -> `std::uint64_t` |            
| Negative fixint | ✅ | ✅ | `std::int8_t` -> `std::int64_t` |                               
| `uint8/16/32/64` | ✅ | ✅ | `std::uint8_t`, `std::uint16_t`, `std::uint32_t`,            
`std::uint64_t` |                                                                           
| `int8/16/32/64` | ✅ | ✅ | `std::int8_t`, `std::int16_t`, `std::int32_t`, `std::int64_t` |                                                                                           
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

## Additional features
- [x] MessagePack ships with `reader`/`writer` for streaming  
- [x] MessagePack RPC designed in event-driven style  
- [x] Implement your own transport for MessagePack RPC
- [x] Disable reflection with `MSGPACK_DISABLE_REFLECT`

## Examples
Examples can be found in the `examples/` directory.