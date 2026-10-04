void Process()
{
    for (auto ready =
         ReadNext(); ready ||
         CanProceedAfterDelay(); ready = ReadNext()) {
        Use(ready);
    }
    if (auto value =
        ReadNext(); value && PassesLongPredicate(value)) {
        Use(value);
    }
    for (auto ready = ReadNext();
         ready || CanProceedAfterDelay(); ready = ReadNext()) {
        Use(ready);
    }
    for (auto x =
         Make([] { return 1; }); x < ALongMaximumCount(); ++x) {
        Use(x);
    }
    for (auto item : ItemsWithAnExtremelyLongExpression()) {
        Use(item);
    }
    for (auto ready =
         ReadNext(); /* keep */ ready ||
         CanProceedAfterDelay(); ready = ReadNext()) {
        Use(ready);
    }
    for (; ExpensiveLongConditionCheckWithContext(context, scheduler); ) {
        Use(context);
    }
    if (IsReady() && HasTime()) {
        Use(1);
    }
}
