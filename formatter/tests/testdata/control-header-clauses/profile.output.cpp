void Drain()
{
    for (auto permitted = CheckCurrentQuota();
         permitted || CanBorrowFromNextWindowAfterExpiration();
         permitted = CheckCurrentQuota())
    {
        ProcessOne();
    }
    if (
        auto status = FetchCurrentStatus(); status && StatusMeetsAdditionalSchedulingConstraintsWithDeferredChecks(status))
    {
        Schedule();
    }
    if (HasPreviousRoute()) {
        Reuse();
    } else if (
        auto route = TryResolveEndpoint(CurrentEndpoint_); route.IsValid() && route.HasReachableNetworkAddressForRequest())
    {
        Commit(route);
    }
}
