void Report()
{
    YT_LOG_INFO("Values %v %v",
        first,
        second);
    YT_TLOG_ERROR(error, "Failure %v",
        value);
    YT_TLOG_DEBUG_IF(ready, "Value %v",
        value);
    YT_TLOG_EVENT(logger, level, "Value %v",
        value);
    YT_TLOG_EVENT_WITH_STATIC_ANCHOR(logger, level, anchor, "Value %v",
        value);
    THROW_ERROR_EXCEPTION(code, "Values %v %v",
        first,
        second);
    THROW_ERROR_EXCEPTION_IF(ready, code, "Values %v %v",
        first,
        second);
    THROW_ERROR_EXCEPTION_UNLESS(ready, "Value %v",
        value);
    auto error = TError(code, "Values %v %v",
        first,
        second)
        .With("Code", code);
    Consume("Values %v %v", first, second);
}
