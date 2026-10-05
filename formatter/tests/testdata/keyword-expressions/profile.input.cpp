bool Fits()
{
    return
        invoker.Get() == this ||
        wrapper->Check(invoker);
}
auto Longer()
{
    return
        firstLongConditionWithDescriptiveName ||
        secondLongConditionWithDescriptiveName ||
        thirdLongConditionWithDescriptiveName;
}
auto Calls()
{
    return
        MakeValue();
}
auto Unary()
{
    return
        *value;
}
auto Conditional()
{
    return
        condition ?
        first :
        second;
}
auto Commented()
{
    return
        // This comment belongs to the returned expression.
        first || second;
}
auto Trailing()
{
    return // This must stay on the keyword's line.
        first || second;
}
auto Block()
{
    return /* This stays inline. */
        MakeValue();
}
auto Coroutines()
{
    co_yield
        MakeValue();
    co_return
        first || second;
}
void Throws()
{
    throw
        MakeError();
}
void BareKeywords()
{
    return;
    throw;
}
auto Protected()
{
    // hit-fmt off
    return
        first ||
        second;
    // hit-fmt on
}
