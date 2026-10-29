void Drain()
{
    for (auto permitted = CheckCurrentQuota();
        permitted || CanBorrowFromNextWindowAfterExpiration();
        permitted = CheckCurrentQuota())
    {
        ProcessOne();
    }
    if (auto status = FetchCurrentStatus();
        status && StatusMeetsAdditionalSchedulingConstraintsWithDeferredChecks(status))
    {
        Schedule();
    }
    if (auto item = storage.find("A Sample Entry");
        item != storage.end() && HasRemainingCapacity() && IsRequestedItemAvailable() &&
        IsDeliveryOptionAvailableForCurrentRegion())
    {
        Use(item);
    }
    switch (auto index = LookupIndex("A Sample Entry");
        index + ExtraCapacityForCurrentMode() + ExtraCapacityForFallbackMode())
    {
        case 0:
            break;
    }
    if (UpdateState([] {
        Touch();
        return 1;
    });
        HasRemainingCapacity() && IsDeliveryOptionAvailable())
    {
        Use(1);
    }
    if (HasPreviousRoute()) {
        Reuse();
    } else if (auto route = TryResolveEndpoint(CurrentEndpoint_);
        route.IsValid() && route.HasReachableNetworkAddressForRequest())
    {
        Commit(route);
    }
}
