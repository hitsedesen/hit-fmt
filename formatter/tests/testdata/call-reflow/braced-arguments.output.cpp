void SaveBatch()
{
    Schedule(
        queue
            .Connect()
            .Drain(),
        [&] (int sequence) {
            WriteRecord(
                destination,
                Bundle<long>{sequence},
                &(cache.Lookup()),
                MergeSources(primaryIndex, secondaryIndex));
        });
    Submit(
        types::Holder<Bundle<long>>{},
        next,
        [] {
            return 1;
        });
    Submit(Bundle<long, bool>{count, ready}, &(cache.Lookup()), finish());
    Submit(
        Bundle<long>{
            [] {
                return 2;
            } (),
        },
        last);
    Submit(ready ? Bundle<long>{first, second} : Bundle<long>{}, finish());
    Submit(left < middle, right > (value), finish());
}
