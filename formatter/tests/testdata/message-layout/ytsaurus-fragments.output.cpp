void F()
{
    THROW_ERROR_EXCEPTION("Short " "message");
}

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
    THROW_ERROR_EXCEPTION(code, "Values " "%v",
        first,
        "Other "
        "argument");
    THROW_ERROR_EXCEPTION(
        "An extended error description that exceeds the column limit once all fragments are combined "
        "with this additional context",
        first);

    YT_LOG_INFO(u8"Short " /* fragment */ u8"message",
        value);

    Consume("Short " "message", value);
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
