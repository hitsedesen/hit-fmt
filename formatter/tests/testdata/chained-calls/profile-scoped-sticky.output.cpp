void WriteAttributes()
{
    Wrap(Builder()
        .BeginMap()
            .Item("host").Value(host)
            .Item("mode").Value(mode)
        .EndMap());
    registrar.Parameter("rate", &TThis::Rate)
        .Default();
    registrar.Parameter("already_split", &TThis::Rate)
        .Default();
    registrar.Parameter("limit", &TThis::Limit)
        .Default(10)
        .GreaterThan(0);
    registrar->Parameter("mode", &TThis::Mode)
        ->Default();
    otherRegistrar
        .Parameter("rate", &TThis::Rate)
        .Default();
    registrar
        .Other("rate")
        .Default();
    registrar.Parameter("aLongDescriptiveSettingName", &TThis::AnotherLongDescriptiveMemberName)
        .Default();
}
