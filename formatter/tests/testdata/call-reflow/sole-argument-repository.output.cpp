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
    Consume(firstLengthyConditionValue || secondLengthyConditionValue ||
        thirdLengthyConditionValue);
    CallWithANameThatLeavesNoRoomForTheCompleteCaptureHeaderOnThisLine(
        [first, second, third] {
            Work();
        });
    Invoke();
    Consume(first, second);
    Consume(
        first,
        [state] {
            state->Pause();
        });
    Consume({
        .Kind = AnchorKind::DelimiterColumn,
        .Fallback = 4,
        .Minimum = 4,
        .StructuralBase = true,
        .Continuation = true,
    });
    Consume(/* options */ Options{
        .Kind = AnchorKind::DelimiterColumn,
        .Fallback = 4,
        .Minimum = 4,
        .StructuralBase = true,
        .Continuation = true,
    });
    Prepare(Consume({
        .Kind = AnchorKind::DelimiterColumn,
        .Fallback = 4,
        .Minimum = 4,
        .StructuralBase = true,
        .Continuation = true,
    }));
    CallWithANameThatLeavesNoRoomForTheCompleteArgumentHeaderOnThisLine(
        OptionsWithAHeaderThatDoesNotFitAfterTheCallOpeningDelimiter{
            firstLengthyValue,
            secondLengthyValue,
        });
    Consume(Options<FirstLengthyDocumentSettings, SecondLengthyDocumentSetting>{
        first,
        second,
    });
    Consume(
        Options<FirstLengthyDocumentSettings, SecondLengthyDocumentSettings>{
            first,
            second,
        });
    Consume({1, 2});
    Consume({});
}
