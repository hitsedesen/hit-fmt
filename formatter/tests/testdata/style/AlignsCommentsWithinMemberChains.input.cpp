void Configure(Builder& builder)
{
    builder.Parameter("capacity", &Settings::Capacity)
        .Default()
        .GreaterThan(0)
        // Keep this limit synchronized with the operating system default.
        // Increasing it requires additional process permissions.
        .LessThanOrEqual(4096);
}
