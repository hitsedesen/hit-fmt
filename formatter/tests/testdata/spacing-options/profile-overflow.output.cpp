template <class... Parts>
void Check(Parts... parts)
{
    static_assert(DependentFlag<Parts...>::Value, "message");
    static_assert(!std::is_convertible_v<VeryLongSourceType, SmartPointer<VeryLongTargetType>>, "Use explicit conversion for this constructed value.");
    static constexpr bool Value = CompareTuple<ElementType>(
        std::make_index_sequence<std::tuple_size<ElementType>::value>());
    Callback<int(int, int)>  handler  = MakeHandler();
    auto& chosen = category == "active" ? activeValues : archivedValues;
    auto& other = status == "pending" ? pendingValues : activeValues;
}
