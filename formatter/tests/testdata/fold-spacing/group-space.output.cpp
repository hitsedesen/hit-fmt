template <class... Parts>
auto Sum(Parts... parts)
{
    auto other = MakeValue(1);
    return ( [part = parts] {
        return part;
    } () + ... );
}

template<class... Types>
void Forward(Types... values) noexcept(( noexcept( G(values) ) && ... )) {}

template<class... Types>
void Reverse(Types... values) noexcept(( ... && noexcept( G(values) ) )) {}

template<class... Types>
void Binary(Types... values) noexcept(( true && ... && noexcept( G(values) ) )) {}

template<class... Types>
void Lambda(Types... values)
{
    auto call = [&]() noexcept(( noexcept( G(values) ) && ... )) { return 0; };
}
