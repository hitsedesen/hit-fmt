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
