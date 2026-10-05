void Example()
{
    UnknownRoot()
        .First()
            .Second().Finish();
    UnknownRoot()
        ->First()
            ->Finish();
    auto exceptionallyLongResultName = UnknownRoot()
        .First()
            .Finish();
    MAKE_TASK()
        .First()
        .Finish();
    Consume(UnknownRoot()
        .First()
            .Finish());
    UnknownRoot()
        .First()
            .Finish();
    object.member;
}
