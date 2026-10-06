void Process()
{
    for (auto ready = ReadNext(); ready || CanProceedAfterDelay();
         ready = ReadNext())
    {
        Use(ready);
    }
    if (auto value = ReadNext();
        value && PassesLongPredicate(value))
    {
        Use(value);
    }
    for (auto ready = ReadNext(); ready || CanProceedAfterDelay();
         ready = ReadNext())
    {
        Use(ready);
    }
    for (auto x = Make([] { return 1; }); x < ALongMaximumCount();
         ++x)
    {
        Use(x);
    }
    for (auto item : ItemsWithAnExtremelyLongExpression()) {
        Use(item);
    }
    for (auto ready = ReadNext();
         /* keep */ ready || CanProceedAfterDelay();
         ready = ReadNext())
    {
        Use(ready);
    }
    for (;
        ExpensiveLongConditionCheckWithContext(context, scheduler); )
    {
        Use(context);
    }
    if (IsReady() && HasTime()) {
        Use(1);
    }
}

template<class T> struct Gate {
    static constexpr bool Enabled = true;
};

template<class T> void InspectConditions(int count)
{
    if constexpr (Gate<T>::Enabled &&
                  HasCapacityForCurrentEndpoint() &&
                  !traits::same<T, T>)
    {
        Use(count);
    }
    if (Gate<T>::Enabled && HasCapacityForCurrentEndpoint()) {
        Use(count);
    }
    if (int (value) = ReadCapacityForCurrentEndpoint()) {
        Use(value);
    }
    if (auto item = ReadNext(); int value = item.Count()) {
        Use(value);
    }
    if (ExternalType value = ReadCapacityForCurrentEndpoint()) {
        Use(value);
    }
    if (std::numeric_limits<ExternalType>::max() != count &&
        HasCapacityForCurrentEndpoint())
    {
        Use(count);
    }
}
