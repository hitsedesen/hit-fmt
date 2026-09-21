void Emit()
{
    auto callback = [] (auto fluent, const auto& counts) {
        fluent
            .BeginMap()
                .DoFor(values, [&] (auto item, const DescriptiveKeyType& keyWithAnIntentionallyLongDescriptiveNameForWrapping) {
                    item
                        .BeginMap()
                            .Value(counts[keyWithAnIntentionallyLongDescriptiveNameForWrapping])
                        .EndMap();
                })
            .EndMap();
    };
}
