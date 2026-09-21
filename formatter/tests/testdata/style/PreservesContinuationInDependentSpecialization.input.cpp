template <class Option>
void Wrapper<
    Option,
    typename std::enable_if_t<std::is_convertible_v<Option&, api::Settings&>>
>::Register()
{ }
