template<class Value, class Signature> struct Setter;
struct Owner { using Type = int; static void Run(int); };
template<class T, class R, class U>
void dispatch()
{
    Setter<T, R(ErrorOr<U>&&)>::Run(promise, callback, move(value));
    Setter<T, R()>::Run(promise, callback);
    scope::Setter<T, R(ErrorOr<U>&&)>::Run(promise, callback, move(value));
    ::Setter<T, R(ErrorOr<U>&&)>::Run(promise, callback, move(value));
    Setter<T, R(Pair<U, T>&&, Box<Box<U>>)>::Run(promise, callback);
    Setter<T, R() > ::Run(promise, callback);
    Setter<T, R(ErrorOr<U>&&)> local;
    Owner::Run(1);
    Owner::Type(number);
    auto comparison = left < right > ::Run();
    Setter<T, (left < right)>::Run(promise, callback);
}
void external_types()
{
    Setter<T, R()>::Run(promise, callback);
    Setter<T, R(ErrorOr<U>&&)>::Run(promise, callback, move(value));
}
