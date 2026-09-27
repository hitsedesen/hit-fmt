void Make()
{
    auto values = MakeGroup(
        {
            MakeCallback([&] {
                return 42;
            })
        },
        2);
    GetDispatcher()
        .Subscribe(MakeCallback([this, observer = MakeObserver(this)] (const VeryLongResponseType<SharedPointer<Message>>& response) {
            Observe(response);
        }));
    auto cancel = RegisterHandler(BIND_NO_PROPAGATE([weakRequest = MakeWeak(request), ticket = MakeTicketForRequest(request), this, weakHandler = MakeWeak(handler)] (const TError& error) {
        Notify(error);
    }));
}
