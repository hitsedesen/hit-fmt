template<class T>
requires Ready<T>
struct Box {
    T value;
};
template<class T>
requires Ready<T>
void prefix(T value) {
    consume(value);
}
template<class T>
void trailing(T value)
requires Ready<T>
{
    consume(value);
}
template<class T>
requires Ready<T>
auto both(T value) -> T
requires Available<T>;
namespace detail {
template<class T>
requires Ready<T> && Available<T> && Compatible<T>
void constrained(T value)
requires Ready<T> && Available<T> && Compatible<T>
{
    consume(value);
}
}
template<class T>
struct Member {
    Member()
    requires Ready<T>
    {
    }
    void run() const
    requires Ready<T>
    {
    }
};
auto lambda = []<class T>(T value)
requires (sizeof(T) > 0)
{
    return value;
};
template<class T>
requires requires(T value) {
    value.touch();
}
void expression(T value)
requires (Ready<T> && requires {
    T{};
})
{
}
template<class T>
concept Check = requires(T value) {
    requires Ready<T>;
    value.touch();
};
template<class T> /* prefix */ requires Ready<T> /* suffix */
void commented(T value);
template<class T> // header
requires Ready<T> // constraint
void lineComment(T value);
template<class T>
void trailingComment(T value)
requires Ready<T> // trailing
{
    consume(value);
}
// hit-fmt off
template<class T> requires   Ready<T> void protectedClause(T value) {}
// hit-fmt on
