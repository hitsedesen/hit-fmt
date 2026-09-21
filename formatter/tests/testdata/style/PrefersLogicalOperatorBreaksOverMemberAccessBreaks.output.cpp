void Run()
{
    if (ready) {
        if (request.LifecycleState == RequestLifecycleState::PendingReview ||
            std::ssize(AvailableExecutionSlots_) > ConfiguredSoftLimit_ - ReservedWorkerSlots_)
        {
            Proceed();
        }
    }
}
