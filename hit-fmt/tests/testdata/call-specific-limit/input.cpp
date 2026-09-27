void Check()
{
    EXPECT_THROW(WaitFor(taskPromise.ToFuture()).ThrowOnError(), TInterruptedRequestException);
}
