template <
    bool Enabled,
#ifdef WITH_LOCATION
    auto Location,
#endif
    class T>
void Invoke(T value);

constexpr Tick operator""_tick(unsigned long long value);
using clocks::operator""_tick;

template <int Size>
void UseLiteral(const char (&literal)[Size]);

template <class... TArgs>
void Forward(TArgs&&... args);

template <class TKey, class TValue>
bool Equal(
    const ::google::protobuf::Map<TKey, TValue>& lhs,
    const ::google::protobuf::Map<TKey, TValue>& rhs);

template <class T, class... Ts>
concept CInvocableForTypes = (requires (T&& invokable) {
    { invokable.template operator()<Ts>() };
} && ...);

void Decode()
{
    Read((char*)&value);
    Map((const void*)&namespace_name::Callback);
    auto convert = [&] <class T> (T value) { return value; };
    new (storage) Value;
    new(storage) Value;
    int shortValue = 1;     // Values stay aligned.
    int longerValue = 100;  // Values stay aligned.

    int flags =
#if defined(FIRST_PLATFORM)
        FirstFlag;
#else
        SecondFlag;
#endif

    errors.push_back(TError(
        "A deliberately long error message remains next to TError while the enclosing call breaks before it to save horizontal space"));
}
