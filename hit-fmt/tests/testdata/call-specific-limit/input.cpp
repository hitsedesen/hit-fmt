void Check()
{
    EXPECT_THROW(WaitFor(notifyReady.ToFuture()).ThrowOnError(), TWorkerLeaseRevokedDuringShutdownException);
}
