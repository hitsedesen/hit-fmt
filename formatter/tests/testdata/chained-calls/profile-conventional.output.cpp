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
