namespace N {

} // namespace N

auto Make() -> Value
{
    return ::sample::Value{};
}

void UseValue()
{
    Use(/*index*/ 0);
    Register({1, 2}, {3, 4});
    Call(
        first,
        false, // useSnapshot
        third);
}
