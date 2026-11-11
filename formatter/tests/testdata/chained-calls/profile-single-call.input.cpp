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
