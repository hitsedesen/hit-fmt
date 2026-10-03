void f() {
    auto result = Select(status ? first : second);
    auto callback = MakeCallback([&] {
        Run();
    });
}
