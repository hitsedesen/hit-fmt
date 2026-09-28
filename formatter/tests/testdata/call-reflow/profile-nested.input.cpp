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

struct DeferredState
{
    DeferredState(const Options& options)
        : token((options.Enabled || options.WaitForReply)
            ? MakeToken()
            : EmptyToken())
    { }
};

void CheckCredentials()
{
    while (running) {
        if (!CredentialsMatch()) {
            Abort(ReadFailure("The selected channel profile cannot be loaded while endpoint handshakes are still pending"));
        }
    }
}

template <class TArchive, class TTag>
void ValidateUpdate(TArchive&& archive, const TTag& tag)
{
    YT_VERIFY(QueryIndex(std::forward<TArchive>(archive), tag) != 0, Explain("A synthetic archive update could not locate the indexed record while validating its state (Archive: %v, Tag: %v)",
        LabelOf<TArchive>(), LabelOf<TTag>()));
}

void CheckFlag(bool flagged)
{
    YT_VERIFY(flagged, Explain("The synthetic telemetry sample was rejected because its status did not match the registered channel description (Flag: %v, State: %v)",
        LabelOf<int>(), LabelOf<char>()));
}

void CheckSignal(bool active)
{
    YT_VERIFY(ValidateSignalPath(active, ReadConfiguredSignalSource()), Explain("The synthetic signal did not match its destination (%v, %v)",
        LabelOf<int>(), LabelOf<char>()));
}

void RejectMissing()
{
    THROW_ERROR_EXCEPTION("A synthetic archive lookup could not locate the requested segment at revision %v for channel %v", revision, channel);
}
