void ReportTimings()
{
    output << RenderSummary(
        "Worker statistics (Elapsed: %v, Ticks/task: %v, Ticks/wakeup: %v)\n",
        NClock::ToDuration(elapsed),
        elapsed / taskCount,
        elapsed / worker.WakeupCount);
    input >> ReadSummary(
        firstExtendedArgumentName,
        secondExtendedArgumentName,
        thirdExtendedArgumentName,
        fourthExtendedArgumentName);
    output << RenderSummary("Ready: %v", ready);
    output << prefix << RenderSummary(
        "Worker statistics (Elapsed: %v, Ticks/task: %v, Ticks/wakeup: %v)\n",
        NClock::ToDuration(elapsed),
        elapsed / taskCount,
        elapsed / worker.WakeupCount);
    channel.SelectedOutputForCurrentWorker().GetBufferedOutputStream()
        << RenderSummary("Worker statistics: %v", elapsed);
    auto mask = initialMask << shiftCount;
}
