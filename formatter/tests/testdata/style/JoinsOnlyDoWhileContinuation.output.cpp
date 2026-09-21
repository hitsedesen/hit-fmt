void Drain()
{
    do {
        Retry();
    } while (HasPendingWork());

    if (shouldReset) {
        Reset();
    }

    while (auto packet = ReadPacket(source)) {
        Handle(packet);
    }
}
