void Example(bool condition)
{
    MAKE_TASK(value).One()
        .Two().Three();
    auto result = condition ? BuildAlternativeWithLongName() : MAKE_TASK(value).Done();
    MAKE_TASK(value)
        .Schedule(worker)
        .Finish();
}
