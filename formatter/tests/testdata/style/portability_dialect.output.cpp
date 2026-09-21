#define PORTABLE_NOEXCEPT noexcept
namespace third_party { template<typename Element> struct Buffer { Buffer(Buffer&& source) PORTABLE_NOEXCEPT : begin(source.begin), end(source.end) { } Element* begin,*end; }; }
