auto client = MakeServiceClient()
    .Configure(mode).Start();
auto expanded = CreateClient()
    .Configure(firstMode)
    .Select(secondMode)
    .Start();
