void Populate()
{
    for (int attempt = 0; attempt < 5; ++attempt)
        //tracker->Record(attempt);
        tracker->Record(17);

    int count = 2;
    if (ready) {
        count = ReadCount();
    }

    int capacity = 4;
    Use(count, capacity);
}
