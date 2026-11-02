struct ValueHolder {
    int value;
    int & Read()&;
    int && Take()&&;
    const int & Read()const&;
    const int && Take()const&&;
    int * Peek()const&&noexcept;
    int & Present()const &;
    int && Carry()const &&;
    int & ReadVolatile()volatile&;
    int && TakeVolatile()const volatile&&;
    int & WithComment()const /* cv qualification */ &;
    void Set(const int & value, const int && temporary);
    std::pair<int, int> operator*() const { return {}; }
    int Tight() const& { return value; }
    int Loose() const  & { return value; }
    int Volatile() const volatile&& { return value; }
    int RefOnly()& { return value; }
    int& Bound() &PROJECT_LIFETIME { return value; }
    int&& MoveBound() && PROJECT_ANNOTATE(lifetime) { return static_cast<int&&>(value); }
    const int& ConstBound() const & PROJECT_LIFETIME PROJECT_ANNOTATE(lifetime) { return value; }
    int& DeclaredBound() & PROJECT_LIFETIME;
    int& NativeBound() & [[clang::lifetimebound]] { return value; }
    int& CommentBound() & /* lifetime */ PROJECT_LIFETIME { return value; }
    operator bool() const;
};
const int & ValueHolder::Read()const& { return value; }
const int && ValueHolder::Take()const&& { return std::move(value); }
int (ValueHolder::*method)()const&&;
inline ValueHolder::operator bool() const { return true; }
