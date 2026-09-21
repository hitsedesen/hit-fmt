void Insert(bool enabled)
{
    records.emplace(key, Record{
        .First = enabled
            ? MakeFirst()
            : std::nullopt,
        .Second = enabled
            ? MakeSecond()
            : std::nullopt,
    });
}
