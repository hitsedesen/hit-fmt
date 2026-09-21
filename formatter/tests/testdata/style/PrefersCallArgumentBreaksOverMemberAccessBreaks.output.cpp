void Run()
{
    if (ready) {
        entry->Position = RoundDown(
            Random_.Next(0, UpperBound_ - Settings_.PageSize),
            static_cast<unsigned long>(Settings_.Granularity));
    }
}
