void Execute()
{
    auto callback = [&] {
        Schedule(BIND([&] {
            auto nested = [&] {
                auto guard = Guard(lock);
                EXPECT_THROW(
                    WaitFor(taskPromise.ToFuture()).ThrowOnError(),
                    TInterruptedRequestException);
                EXPECT_THROW(CheckNow(), TInterruptedRequestException);
                OTHER_ASSERT(WaitFor(taskPromise.ToFuture()).ThrowOnError(), TInterruptedRequestException);
                auto outstanding = started - completed;
                THROW_ERROR_EXCEPTION_UNLESS(
                    outstanding <= configuredLimit,
                    "Outstanding work units %v exceed the requested maximum number of tasks %v",
                    outstanding,
                    configuredLimit);
            };
        }).AsyncVia(invoker).Run());
    };
}

void CheckCredentials()
{
    while (running) {
        if (!CredentialsMatch()) {
            Abort(ReadFailure(
                "The private key associated with this connection does not match the selected certificate"));
        }
    }
}

void EmitRecords()
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("entries").DoMapFor(recordsWithAttachedStatistics, [] (auto fluent, const auto& recordWithAttachedStatistics) {
                auto [record, statistics] = recordWithAttachedStatistics;
                fluent.Item("id").Value(record.GetId());
            })
        .EndMap();
}
