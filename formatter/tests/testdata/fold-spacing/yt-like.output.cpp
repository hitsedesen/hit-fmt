template <class... Parts>
auto Sum(Parts... parts)
{
    auto other = MakeValue(1);
    return ([part = parts] {
        return part;
    } () + ...);
}
