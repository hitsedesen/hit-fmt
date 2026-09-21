void function(size_t actionCount, Duration delay)
{
    auto task = make<Worker>(
        bind([&] (size_t index) {
            auto value = values[index % 16];
            return queues[value]->getInvoker();
        }),
        signal,
        actionCount,
        delay);
    task->invoke(actionCount);
}
