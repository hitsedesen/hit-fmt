void Emit(double seconds)
{
    Output << LoadedItems_ / 1000 << "k, "
           << StoredItems_ / 1000 << "k, "
           << LoadedItems_ / seconds / 1000 << "k/s, "
           << StoredItems_ / seconds / 1000  << "k/s, ";
}
