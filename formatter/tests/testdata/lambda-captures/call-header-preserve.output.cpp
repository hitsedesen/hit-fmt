void Check()
{
    FooBar::ForEachFooBarium([&] (FooBarType /*type*/, auto FooBar::* foobar) {
        result = result && this->*foobar == 0;
    });
    FooBar::ForEachFooBarium([&] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; });
    FooBar::ForEachFooBarium([=] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; });
    FooBar::ForEachFooBarium([this] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; });
    FooBar::ForEachFooBarium([] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; });
    FooBar::ForEachFooBarium([state, count] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; });
    FooBar::ForEachFooBarium([state = MakeState(), count] (
        int value) { return result; });
    Wrap(FooBar::ForEachFooBarium([&] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; }));
    Visit([&] (
        FooBarType /*type*/,
        auto FooBar::* foobar) { return result; }, options);
    Short([&] { return result; });
    Short([&] { result = result && valid; });
    auto index = values[position];
}
