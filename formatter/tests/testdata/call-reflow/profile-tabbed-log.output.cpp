void LogStatus()
{
    YT_TLOG_DEBUG("Dispatch state has switched for route %v with version %v and pending queue %v in epoch %v",
        Route,
        Version,
        Pending,
        Epoch);
}
