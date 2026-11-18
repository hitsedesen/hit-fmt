class TInvoker
{
public:
    void Invoke()
    {
        return bound
            ->Handler_(NHelper::Unpack(std::get<SavedIndexes>(bound->SavedArgs_))..., std::forward<TTs>(args)...);
    }
};

void Invoke()
{
    return bound
        ->Handler_(NHelper::Unpack(std::get<SavedIndexes>(bound->SavedArgs_))..., std::forward<TTs>(args)...);
}

class Queue
{
public:
    void Update()
    {
        while (!pending_.try_replace_if_current(result->previous, result, Ordering::PublishOnSuccess, Ordering::IgnoreOnFailure))
        { }
        while (!pending_.try_advance(next, expected))
        { }
        while (!pending_.try_replacing_the_pending_record_only_when_the_previous_revision_still_matches_the_expected_revision(item))
        { }
    }
};
