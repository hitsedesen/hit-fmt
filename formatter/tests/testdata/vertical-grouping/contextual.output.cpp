void Dispatch(bool ready) {
    YT_VERIFY(ready);
    YT_VERIFY(IsOpen());

    Prepare();

    YT_TLOG_DEBUG("Prepared").With("Count", Count()).With("State", State());

    Deliver();

    // Check the outgoing state.
    YT_VERIFY(IsReady()); // Keep the explanation with the check.

    Finish();

    YT_LOG_INFO("Finished");

    FinishLogging();

    YT_TLOG_CUSTOM("Custom");

    FinishCustomLogging();

    if (ready) {
        YT_VERIFY(IsOpen());

        Run();

        YT_VERIFY(IsReady());
    } else if (Retry()) {
        RetryOnce();
    } else {
        Stop();
    }

    FinishBranch();

    while (Pending()) {
        Poll();
    }

    FinishLoop();

    for (int i = 0; i < 2; ++i) {
        Step(i);
    }

    {
        Guard();
    }

    AfterScope();
    do {
        Drain();
    } while (Pending());

    AfterDrain();
    try {
        Send();
    } catch (...) {
        Recover();
    }

    AfterTry();

    if (ready)
        Run();
    AfterUnbraced();
    int values[] = {1, 2};
    AfterInitializer();

    [[likely]] if (ready) {
        Run();
    }

    AfterAttribute();
    switch (Select()) {
        case 1:
            YT_VERIFY(ready);

            Use();
            break;
        default:
            if (ready) {
                UseDefault();
            }
    }

    AfterSwitch();
    auto check = YT_VERIFY(ready);
    Consume(YT_VERIFY(ready));
    object.YT_VERIFY(ready);
    Other::YT_VERIFY(ready);
    YT_VERIFY_EXTRA(ready);
    return YT_VERIFY(ready);
}

void Edges() {
    Start();
    auto ready = IsReady();

    if (ready) {
        Run();
    }

    // The next loop owns this comment.
    for (;;) {
        YT_VERIFY(IsReady());
    }
}
void Barriers() {
    YT_VERIFY(IsReady());
#if ENABLED
    YT_VERIFY(IsOpen());

    Send();
#endif
    Complete();

    YT_VERIFY(IsReady());

    // An independent section.

    Send();
    // hit-fmt off
    YT_VERIFY( IsOpen() );
    Send( );
    // hit-fmt on
    Complete();
    auto callback = [] {
        YT_VERIFY(IsOpen());

        Send();

        YT_VERIFY(IsReady());
    };
    Use(callback);
}
namespace Inner {
void Local() {
    Run();
}
}

void AfterNamespace();
extern "C" {
void External();
}

void AfterLinkage();
