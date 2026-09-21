void Advance(CounterHandle& counter, SlotHandle& slot, int value, int scale)
{
    if (counter) {
        ++*counter;
        --*counter;
    }
    auto current = **slot;
    auto weighted = value++ * scale;
    Use(current, weighted);
}
