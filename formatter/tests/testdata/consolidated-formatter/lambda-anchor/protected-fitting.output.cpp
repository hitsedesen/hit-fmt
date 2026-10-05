void Recover()
{
    auto callback = [state] (int requestId, int additionalRequestId) -> Result {
        UnknownValue value;
        auto chosenId = requestId;
        if (chosenId > 0) {
            return value;
        }
        return value;
    };
    Use(callback);
}

void RecoverLater()
{
    auto callback = [state] (int requestId) {
        Prepare();
        UnknownValue value;
        Finish(value);
    };
}

using KnownValue = int;
void Known()
{
    auto callback = [state] (int requestId) {
        KnownValue value;
        Finish(value);
    };
}
