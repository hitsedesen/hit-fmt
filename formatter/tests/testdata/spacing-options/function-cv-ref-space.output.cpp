struct ValueHolder {
    int value;
    int& Read()&;
    int&& Take()&&;
    const int& Read() const &;
    const int&& Take() const &&;
    int* Peek() const && noexcept;
    int& Present() const &;
    int&& Carry() const &&;
    int& ReadVolatile() volatile &;
    int&& TakeVolatile() const volatile &&;
    int& WithComment() const /* cv qualification */ &;
    void Set(const int& value, const int&& temporary);
    std::pair<int, int> operator*() const {
        return {};
    }
    int Tight() const & {
        return value;
    }
    int Loose() const & {
        return value;
    }
    int Volatile() const volatile && {
        return value;
    }
    int RefOnly()& {
        return value;
    }
    operator bool() const;
};
const int& ValueHolder::Read() const & {
    return value;
}
const int&& ValueHolder::Take() const && {
    return std::move(value);
}
int (ValueHolder::*method)() const &&;
inline ValueHolder::operator bool() const {
    return true;
}
