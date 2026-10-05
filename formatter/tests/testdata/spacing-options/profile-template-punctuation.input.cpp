template <class T, class R>
void Complete(T promise, R callback)
{
    CallbackSetter<T, R()>::Apply(promise, callback);
    CallbackSetter<
        T,
        R()>::Apply(promise, callback);
    CallbackSetter<T, R() > ::Apply(promise, callback);
    CallbackSetter<T, R (*)(int) > ::Apply(promise, callback);
}

template <class T>
struct PoolTraits<T, typename std::enable_if_t<std::is_convertible_v<T&, ::wire::Message&> > >
{};

using Nested = Box<Box<Box<int> > >;
using Mixed = Box<Box<Box<int>> >;
using Reverse = Box<Box<Box<int> >>;
using Adjacent = Box<Box<Box<Box<int>> >>;
using Commented = Box<Box<int> /* closer */ >;
using Global = Box< ::wire::Message>;

void Compare()
{
    auto greater = value > ::limits::Maximum;
    auto shifted = value >> ::limits::Bits;
    auto expression = Box<(value >> 2)>();
    // An unresolved expression head remains conservative.
    auto nested = Box<Box<int> >::Make();
    // hit-fmt off
    Box<Box<int> > ::Make();
    // hit-fmt on
}
