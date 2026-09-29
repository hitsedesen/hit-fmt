void f() {
    YT_LOG_INFO("A long enough message to require wrapping",
        firstArgument,
        secondArgument);
    YT_LOG_INFO("Failed to initialize " "the storage client", clientId, retryCount);
    YT_TLOG_ERROR(error, "Failure (%v)", firstArgument);
    YT_TLOG_ERROR(
        error,
        "Failure with enough detail to require wrapping",
        firstArgument);
    Other(
        "A long enough message to require wrapping",
        firstArgument,
        secondArgument);
    YT_LOG_INFO_CUSTOM(
        "A long enough message to require wrapping",
        firstArgument,
        secondArgument);
    YT_LOG_INFO_IF(
        enabled,
        "A message requiring wrapping",
        firstArgument,
        secondArgument);
}
void g() { YT_LOG_INFO("Message",
    count); }
void h() { YT_LOG_INFO("Message"); }
void i() { YT_LOG_INFO(
    "Failed to load " "configuration",
    configPath); }
