template<class Key>
struct Bounds {
    template<class Ref>
        requires(!Convertible<Ref, Key> &&
            !Same<RemoveReference<Ref>, NullValue>)
    Bounds(Tag, Ref&& key);
};

template<class T>
    requires(Readable<T> && Serializable<T> && !EmptyType<T>)
void Process(T value);

template<class T>
void Save(T value)
    requires(Readable<T> && Serializable<T> && !EmptyType<T>);

template<class Key, class Ref>
    requires(!Convertible<Ref, Key> &&
        !Same<RemoveReference<Ref>, NullValue>)
void Insert(Ref&& value);

template<class Key, class Ref>
void Erase(Ref&& value)
    requires(!Convertible<Ref, Key> &&
        !Same<RemoveReference<Ref>, NullValue>);
