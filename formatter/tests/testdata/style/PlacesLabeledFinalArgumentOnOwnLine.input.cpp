void function()
{
    waitFor(
        service->advance(
            consumer,
            queue,
            partition,
            oldOffset,
            newOffset, /*options*/ {}));
}
