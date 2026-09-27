void Execute()
{
    auto callback = [&] {
        Schedule(BIND([&] {
            auto nested = [&] {
                auto guard = Guard(lock);
                EXPECT_THROW(WaitFor(pipelineReady.ToFuture()).ThrowOnError(), TQuotaExpiredException);
                EXPECT_THROW(CheckQuota(), TQuotaExpiredException);
                OTHER_ASSERT(WaitFor(pipelineReady.ToFuture()).ThrowOnError(), TQuotaExpiredException);
                auto outstanding = started - completed;
                THROW_ERROR_EXCEPTION_UNLESS(outstanding <= configuredLimit, "Processing window %v cannot accept an additional allocation when quota %v has already been reached",
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
            Abort(ReadFailure("The selected channel profile cannot be loaded while endpoint handshakes are still pending"));
        }
    }
}
