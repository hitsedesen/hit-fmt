template<class T> struct Parcel
{
    explicit Parcel<T>(Item<T>* value)
    {
        use(value, 1);
    }
    Parcel<T>(Item<T>& value): saved(value)
    {
        use(value, 2);
    }
};
template<class T> Parcel<T> ::Parcel(Item<T>* value)
{
    use(value, 3);
}
template<class T> Item<T> make(Item<T>* value)
{
    return *value;
}
int (*callback)(int value);
