void Run()
{
    auto callback = [factory = [] (int seed) {
        return [seed] (int value) {
            return seed + value;
        };
    }]
    (int value) {
        return factory(value)(value);
    };
    auto next = [&callback] (int value) {
        return callback(value);
    };
}
