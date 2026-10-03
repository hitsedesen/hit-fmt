[[nodiscard]] constexpr std::optional<ResultWithSize>
ReadFirstResultFromPaddedStorage(const char* input)
{
    return {};
}
[[nodiscard]] constexpr std::optional<ResultWithSize>
ReadFirstResultFromPaddedStorage(const char* input);
[[nodiscard]] [[deprecated("use Read")]] static constexpr unsigned long long
ReadVeryLongCounterFromInput(const char* input);
constexpr decltype(Probe(First{}, Second{})) ReadComputedResultFromInput();
template <class Element>
[[nodiscard]] constexpr Container<Element> ConstructOneContainerForInput(
    Element element)
{
    return {};
}
class Reader {
    public:
    [[nodiscard]] virtual const ResultWithSize& ReadResultFromCurrentStorage()
    const noexcept = 0;
    [[nodiscard]] static auto GetResultFromCurrentStorage() noexcept
    -> std::optional<ResultWithSize>;
    Reader() noexcept;
    operator bool() const noexcept;
};
[[nodiscard]] constexpr int Read() {
    return 1;
}
[[nodiscard]] constexpr std::optional<ResultWithSize> ReadFirstResult(
    const char* input);
template <>
text::CodePoint detail::Cursor<Reader>::Read() {
    return {};
}
template <class T>
text::CodePoint detail::Cursor<Box<T>>::Read() {
    return {};
}
text::CodePoint detail::Cursor<Reader>::Read();
