void EmitRecords()
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("routes").DoMapFor(
                routesWithMetrics,
                [] (auto fluent, const auto& routeWithMetrics) {
                    auto [route, metrics] = routeWithMetrics;
                    fluent
                        .Item("key").Value(route.GetKey());
                })
        .EndMap();
}

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

void Connect()
{
    if (!future.IsSet()) {
        future.Subscribe(Bind(&TRunner::OnResult, MakeStrong(this), index)
            // The synchronous invoker prevents recursive completion.
            .Via(GetSyncInvoker()));
        future.Subscribe(Bind(callback)
            // First explanation.
            // Second explanation.
            .Via(invoker)
            .Async());
        future.Subscribe(Bind(callback) // Trailing comment stays attached.
            .Via(invoker));
        future.Subscribe(Bind(callback)
            /* This explains the continuation. */
            .Via(invoker));
        future.Subscribe(Bind(callback) /* Inline comment. */.Via(invoker));
        auto value = Builder()
            .BeginMap()
                // A nested fluent item.
                .Item("key").Value(value)
            .EndMap();
        registrar
            // The parameter selector belongs to the receiver.
            .Parameter("rate", &TThis::Rate)
            .Default();
    }
}
void Finalize()
{
    Submit([this, error = std::move(pendingError)] () mutable {
#ifdef ENRICH_ERROR
        // Symbolization is delegated to the finalizer.
        error.Add("origin", Symbolize(trace));
#elif SHORT_ERROR
        error.Add("code", code);
#else
        error.Clear();
#endif
        Finish(error);
    });
    Submit([] {
    #ifdef KEEP_AUTHORED_INDENT
        Work();
    #endif
    });
    Submit([] {
        Prepare();
#if FIRST_PATH
        Work();
#endif
    });
}

void Run()
{
    state
        .DeferredCallbacks
        ->front()
        ();
    state
        .DeferredCallbacks
        ->front()
        (argument);
    Receiver()
        .Open()
        ->Close();
    Receiver()
        ->Open()
        .Close();
    Receiver()
        .Open()
        ();
    Receiver()
        ->Open()
        ();
    receiver
        .Open()
        .Close();
    receiver
        ->Open()
        ->Close();
    Factory()
        (1)
        (2);
    receiver.Field.Child.Open();
    receiver->Field->Child->Open();
    receiver.Field->Child.Value;
    receiver
        .First()
        .Second()
        ->Third();
    receiver
        ->First()
        ->Second()
        .Third();
    receiver
        .First()
        ->Second()
        .Third()
        ->Fourth();
    return Factory()
        .Open()
        ->Close()
        .Finish();
    receiver
        .First()
        .Field
        .Second();
    receiver
        .First()
        .Second()
        (argument);
    Receiver()
        .First()
        // This does not change the link kind.
        .Second();
    namespace_name /* Keep this qualifier comment. */::Factory()
        .First()
        .Second();
    state
        .aVeryLongDescriptiveFieldNameWithCallbacks
        ->front()
        (aVeryLongDescriptiveArgumentName);
}

void Receive()
{
    transfer
        ->Run(std::move(chunks))
        .AsUnique()
        .Subscribe(Bind(
            [sink = std::move(sink), prefix = std::move(prefix), channel = std::move(channel), requestId] (
                TErrorOr<std::vector<TSharedRef>>&& result) mutable {
                    if (!result.IsOK()) {
                        // A failed transfer has nothing to send.
                        return;
                    }

                    // Combine the initial data with the transferred data.
                    auto& data = result.Value();
                    TSharedRefArrayBuilder builder(prefix.Size() + data.size());

                    for (const auto& part : prefix) {
                        builder.Add(part);
                    }

                    for (auto& part : data) {
                        builder.Add(std::move(part));
                    }

                    sink->Send(builder.Finish(), std::move(channel), nullptr, requestId);
                }));
}

void Schedule()
{
    return BIND([=, this, self = Hold(this), task = std::forward<Task>(task), ... items = std::move(items)] {
        return Dispatch(std::move(task), std::forward<Items>(items)...);
    })
        .AsyncVia(executor)
        .Run();
}

void RecoveredArguments()
{
    Wrap([] {
        Dispatch(items...);
    })
        .First()
        .Second();
    Wrap([] {
        Dispatch(items...);
    })
        ->First()
        .Second();
    Wrap([] {
        Dispatch(items...);
    })
        .First()
        (value);
    Receiver(items...)
        .First()
        .Second();
    Receiver(items...).Only();
    Receiver(items...).Field.Child;
    std::forward<Items>(items)
        .First()
        .Second();
}

void Events()
{
    YT_TLOG_TRACE("Queue is active")
        .With("Count", count);

    YT_LOG_DEBUG("Queue is idle")
        .With("Count", count);

    THROW_ERROR_EXCEPTION("No capacity")
        .With("Capacity", capacity);

    LOG_EVENT("Queue is active").With("Count", count);

    YT_TLOG_TRACE("No suffix");

    auto error = TError("Invalid request")
        .With("Code", code);
}
