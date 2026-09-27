template <class... Parts>
void Check(Parts... parts)
{
    static_assert(DependentFlag<Parts...>::Value, "message");
    static_assert(!std::is_convertible_v<VeryLongSourceType, SmartPointer<VeryLongTargetType>>, "Use explicit conversion for this constructed value.");
    static constexpr bool Value = CompareTuple<ElementType>(std::make_index_sequence<std::tuple_size<ElementType>::value>());
    Callback<int(int, int)>  handler  = MakeHandler();
    auto& chosen =
        category == "active" ?
        activeValues :
        archivedValues;
    auto& other =
        status == "pending"
        ? pendingValues
        : activeValues;
}

EMode ResolveMode()
{
    return configuredMode.value_or(selectedEncoding == EEncoding::HumanReadable ? EMode::ReadFriendlyFormat : EMode::CompactBinaryFormat);
}

void ConfigureEndpoint()
{
    Endpoint endpoint;
    endpoint =
        Endpoint::CreateSocketFromConfigPath(Paths::ResolveNormalizedAbsolutePath(*settings.PreferredNetworkSocketPath));
    auto unchanged =
        MakeMode(shortCode);
}

void ResolvePath(const SomeVeryLongPath& path, Request* /*request*/, Response* /*response*/, const SomeVeryLongContextPointer& context)
{
    Use(path);
}
