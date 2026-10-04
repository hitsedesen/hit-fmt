void Compare()
{
    auto previousChannelModeFromLegacyMask =
        static_cast<EChannelMode>(ControlBits_.fetch_and(~static_cast<uint64_t>(EChannelMode::Sleeping)));
    auto inner = static_cast<Tiny>(CalculateLongReachableIdentifierStateFromCachedValue(
        LongArgumentRepresentingCurrentEnvironmentAndScheduler));
    auto mustStay =
        static_cast<ELarge>(Make(/* preserve */ VeryLongArgumentRepresentingTheEntireSchedulerStateAndCurrentWorker));
    auto withTernary = static_cast<ELarge>(condition
        ? FetchValueFromPrimaryProcessorWithLongName()
        : FetchValueFromSecondaryProcessorWithLongName());
    if (!channelControl.compare_exchange_weak(
        previousObservedControl,
        static_cast<unsigned long long>(pendingStatusBits | receivedControlSignals)))
    {
        Retry();
    }
    if (!channelControl.compare_exchange_weak(
        previousObservedControl, /* keep */ static_cast<unsigned long long>(pendingStatusBits |
            receivedControlSignals)))
    {
        Retry();
    }
    if (Any(previousChannelStatus & ~EChannelStatus::CurrentlyProcessing) &&
        None(previousChannelStatus & EChannelStatus::PendingShutdown))
    {
        Handle();
    }
    if (Find(GetAvailableSerializationFormatIdentifiers(), format) !=
        GetAvailableSerializationFormatIdentifiers().end())
    {
        Use();
    }
    if (Find(GetAvailableSerializationFormatIdentifiers(), GetAlternativeSerializationFormatIdentifiers()) != expected)
    {
        UseAlternative();
    }
}

void StoreRecord()
{
    Values_.push_back(std::string(
        Options_->Lead,
        '.') + std::string(DescribeArchivedRecordPayload(DecodeRecordVariant<TSubstantialRecordPayload>(
        record.Payload))));
    auto product = ComputeLedgerWeight(
        factors.Base,
        factors.Limit) * ConvertTokenizedMetric(ExtractValueFromIndexedPayload<TStoredMetric>(record.Payload));
}
