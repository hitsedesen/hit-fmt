template<class T>
    requires library::Ready<T>
Envelope<T> inspect(T value) {
    return {};
}

template<class T>
    requires (Ready<T> && !Blocked<T>)
library::Envelope<T> collect(const T& value) {
    return {};
}

template<class T>
    requires requires(T value) {
        value.touch();
    } or (left & mask)
Envelope<Envelope<T>> Owner::inspect(T value) {
    return {};
}

template<class T>
    requires Ready<T> and /* operand */ Enabled<T> /* boundary */
Envelope<T> Owner::operator()(T value) {
    return {};
}

struct Converter {
    template<class T>
        requires(Ready<T> && !Blocked<T>)
    operator T() const noexcept {
        return T{};
    }

    template<class T>
        requires Ready<T>
    operator const T&() const;

    template<class T>
        requires requires {
            typename T::value_type;
        }
    explicit operator T*() const noexcept {
        return nullptr;
    }
};

int outside;
