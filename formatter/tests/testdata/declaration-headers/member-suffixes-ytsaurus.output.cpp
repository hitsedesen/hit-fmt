const TSnapshotReader::TStoredReadingSnapshot&
TRemoteSensorReader::ReadCurrentSnapshotFromPrimaryStorage() const noexcept
{
    return Snapshot_;
}

const TReadingSnapshot& TRemoteSensorReader::ReadSnapshot(
    const TSnapshotReadOptions& options,
    TInstant timestamp) const noexcept
{
    return Snapshot_;
}

const TSnapshotReader::TStoredReadingSnapshot& TRemoteSensorReader::ReadCurrentSnapshot() const noexcept(noexcept(
    ReadSnapshotImpl()))
{
    return ReadSnapshotImpl();
}

struct ISnapshotReader
{
    virtual const TSnapshotReader::TStoredReadingSnapshot&
    ReadCurrentSnapshotFromPrimaryStorageWithFallbackToCache() const noexcept = 0;

    int Value() const noexcept
    {
        return 1;
    }

    const TReadingSnapshot& Read() const volatile & noexcept
    {
        return Snapshot_;
    }

    explicit operator bool() const noexcept
    {
        return true;
    }

    auto Copy() const noexcept -> TSnapshotReader
    {
        return {};
    }

    int Commented() // Keep this line.
    const noexcept
    {
        return 2;
    }
};
