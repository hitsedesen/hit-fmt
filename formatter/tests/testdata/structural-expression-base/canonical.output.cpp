void inspect() {
    auto result = fetch(
        1,
        2
    );
    fetch(
        1,
        2
    );
    auto worker = []() {
        // Keep the explanation with the nested body.
        if (true) {
            auto inner = fetch(
                3,
                4
            );
        }
        return 0;
    };
}
