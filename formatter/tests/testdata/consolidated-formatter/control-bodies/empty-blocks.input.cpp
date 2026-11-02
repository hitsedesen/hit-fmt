void spin(bool a) {
    while (a);
    while (
        a);
    for (; a; );
    do; while (a);
    if (a); else Stop();
    if (a) /* keep */ ;
}

void retry() {
    auto remaining = counter.load();
    // Retry without changing a negative count.
    while (remaining > 0 && !counter.exchange(remaining, 0))
        ;

    PendingItem item;
    for (; ready(); );
    PendingItem next;
    if (ready());
    PendingItem last;
    while (ready()) work();
    PendingItem afterWork;
}

void template_conditions() {
    while (TryProcessRequests(std::numeric_limits<int>::max(), true))
        ;
    if (Ready(limits<unsigned long>::max(), enabled));
    for (; Ready(factory<const Item*>::make(), true); );
    do; while (Ready(limits<int>::max(), true));
    while (Ready(limits<int>::max(), true)) Work();
    while (Ready(left < right > ::value(), true))
        ;
    while (Ready(limits<int>::max(), true)) /* keep */ ;
}
