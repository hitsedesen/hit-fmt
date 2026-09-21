template <class... Values>
struct ValueCollection
{
    static constexpr bool Distinct = DistinctConcept<Values...>;
    static constexpr bool Offset = OffsetConcept<sizeof...(Values) + 1>;

    template <class Needle>
    static constexpr bool Contains = OneOfConcept<Needle, Values...>;
};
