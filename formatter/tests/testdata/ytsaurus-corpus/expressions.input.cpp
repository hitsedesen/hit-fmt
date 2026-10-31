namespace NYT::NProfileExample {

struct TOptions
{
    int Capacity;
    int RetryCount;
};

const int BucketSizes[] = {
    3, 6, 12, 24,
    48, 96, 192, 384,
};

const TOptions DefaultOptions{
    .Capacity = 16,
    .RetryCount = 3,
};

const TOptions CompactOptions{8, 1};

void ReportStatus(const TLogger& logger, int level, bool ready, int attempt, const TAnchor& anchor)
{
    YT_TLOG_EVENT(logger, level, "Status updated").With("Attempt", attempt);
    YT_TLOG_DEBUG_IF(ready, "Status accepted").With("Attempt", attempt);
    YT_TLOG_DEBUG_UNLESS(ready, "Status pending").With("Attempt", attempt);
    YT_TLOG_FATAL_IF(!ready, "Status rejected").With("Attempt", attempt);
    YT_TLOG_EVENT_WITH_STATIC_ANCHOR(logger, level, anchor, "Status recorded").With("Attempt", attempt);
}

void InspectState(TProvider& provider, TProfiler& profiler, TRegistrar registrar)
{
    auto result = provider.GetState().GetValue();
    auto counter = profiler.WithTag("phase", "prepare").Counter("/attempts");
    auto map = BuildYsonStringFluently().BeginMap().Item("capacity").Value(16).EndMap();
    auto node = NYTree::BuildYsonNodeFluently().BeginMap().Item("retries").Value(3).EndMap();
    registrar.Parameter("capacity", &TOptions::Capacity).Default(16);
    registrar.ExternalClassParameter("retry_count", &TOptions::RetryCount).Default(3);
}

void CompleteRequest(TFuture<void> future, TFuture<int> valueFuture, IInvokerPtr invoker)
{
    WaitFor(future).ThrowOnError();
    auto value = WaitFor(valueFuture).ValueOrThrow();
    THROW_ERROR_EXCEPTION_IF(value < 0, "Invalid response").With("value", value);
    THROW_ERROR_EXCEPTION_UNLESS(value < 100, "Response too large").With("value", value);
    auto logger = Logger.WithTag("Request: %v", value);
    BIND(&TWorker::Dispatch, worker).Via(invoker).Run();
    BIND([] { Dispatch(); }).AsyncVia(invoker).Run();
    auto immediate = [] { return 7; }();
    switch (value) {
        case 0:
        case 1:
            Dispatch();
            break;
        case 2: {
            auto retry = value + 1;
            Dispatch(retry);
            break;
        }
        default:
            Cancel();
    }
}

} // namespace NYT::NProfileExample
