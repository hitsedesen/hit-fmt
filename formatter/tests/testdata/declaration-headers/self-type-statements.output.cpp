template <class T> struct Observer
{
    template <class U> Observer& Assign(const Handle<U>& value)
    {
        static_assert(Compatible<U*, T*>, "compatible types required");
        Observer(value).Exchange(*this);
        return *this;
    }
    Observer& Clear()
    {
        static_assert(Compatible<T*, T*>, "compatible types required");
        Observer().Exchange(*this);
        return *this;
    }
    void Inspect()
    {
        static_assert(Compatible<T*, T*>, "compatible types required");
        Observer(local);
    }
};
