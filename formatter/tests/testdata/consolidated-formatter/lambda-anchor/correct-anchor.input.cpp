void f()
{
    Visit(
        MakeCallback([&] (auto value) {
        if (value) {
            Use(value);
        }
    }),
        other);
}
