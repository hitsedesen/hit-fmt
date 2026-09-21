void Build(int seed)
{
    auto callback = [
            seed
        ] {
            return Record {
                .Value = seed,
                .Enabled = true
            };
        };
}
