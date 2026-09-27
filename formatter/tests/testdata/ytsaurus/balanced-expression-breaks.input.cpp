void Compare()
{
    auto previousPendingControl = static_cast<EPollControl>(PendingControl_.fetch_and(~static_cast<ui64>(EPollControl::Offline)));
    auto inner = static_cast<Tiny>(CalculateLongReachableIdentifierStateFromCachedValue(LongArgumentRepresentingCurrentEnvironmentAndScheduler));
    auto mustStay = static_cast<ELarge>(Make(/* preserve */ VeryLongArgumentRepresentingTheEntireSchedulerStateAndCurrentWorker));
    auto withTernary = static_cast<ELarge>(condition ? FetchValueFromPrimaryProcessorWithLongName() : FetchValueFromSecondaryProcessorWithLongName());
}
