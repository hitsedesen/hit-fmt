void Emit(Output& output)
{
    output.Write(Builder()
        .BeginMap()
            .Item("values")
                .Do([&] (auto fluent) {
                    fluent.Value(Read());
                })
        .EndMap());
}
