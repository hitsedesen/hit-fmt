void Schedule(auto& values)
{
    auto wrapped = [
            &values,
            guard = Guard([&values]() noexcept {
                                                    values.push_back(1);
                                                })
        ]() mutable {
            values.push_back(2);
        };
    auto direct = [callback = [&values] {
                                                    values.clear();
                                                }, count = 2] {
            callback();
        };
    auto parenthesized = [&callback = Store([&values](int value) {
                                                    if (value) {
                                                        values.push_back(value);
                                                    }
                                                })] {
            callback(3);
        };
    auto directParen = [callback([&values] {
                                                    values.clear();
                                                })] { callback(); };
    auto directBrace = [callback{[&values] {
                                                    values.clear();
                                                }}] { callback(); };
    auto recursive = [callback = [nested = [&values]() noexcept {
                                                    values.clear();
                                                }] {
                                                    nested();
                                                }] { callback(); };
    auto ordinary = [&values]() noexcept {
                                                    values.clear();
                                                };
    auto simple = [&values, count = 1, copy = (count = 2)] { return copy; };
}
