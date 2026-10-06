typename AsyncStore<Key, Value, Hash>::Entry& AsyncStore<Key, Value, Hash>::Entry::operator=(Entry&& other) noexcept;
typename AsyncStore<Key, Value, Hash>::Entry&
AsyncStore<
    Key,
    Value,
    Hash>::Entry::operator=(Entry&& other) noexcept { return *this; }
typename AsyncStore<Key, Value, Hash>::Entry* const AsyncStore<Key, Value, Hash>::Find(int key);
void AsyncStore<Key, Value, Hash>::Clear();
AsyncStore<Key, Value, Hash>::AsyncStore(int capacity) {}
Result Cache<ExceptionallyLongKeyTypeName, ExceptionallyLongValueTypeName>::Fetch(int key);
Result Cache<ExceptionallyLongKeyTypeName, ExceptionallyLongValueTypeName>::NestedEntry::Fetch(int key);
