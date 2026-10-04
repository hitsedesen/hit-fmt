void Example()
{
    auto callback = MakeCallback([state, next] {
        state->Pause();
        next();
    });
    auto guarded = RunSetter<true>(
        [&] {
            Store(value);
        },
        std::move(guard));
    auto nested = Prepare(MakeCallback([state] {
        state->Pause();
    }));
    MakeCallback(/* callback */ [state] {
        state->Pause();
    } /* done */);
    Consume(firstLengthyConditionValue || secondLengthyConditionValue || thirdLengthyConditionValue);
    CallWithANameThatLeavesNoRoomForTheCompleteCaptureHeaderOnThisLine([first, second, third] {
        Work();
    });
    Invoke();
    Consume(first, second);
    Consume(first, [state] {
        state->Pause();
    });
}
