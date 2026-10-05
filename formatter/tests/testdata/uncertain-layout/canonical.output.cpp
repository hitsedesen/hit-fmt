template<class T> struct Box {
};
struct Second {
};

void Example()
{
    auto callback = [] {
        Start();
        auto result = unknown<External>(
            firstArgument,
            secondArgument);
        value = unknown<External>(
            firstArgument,
            secondArgument);
        Finish(result);
    };
    auto inlineCallback = [] { Start(); Finish(); };
    auto options = Options{
        .child = {
            .value = unknown<External>(
                firstArgument,
                secondArgument),
            .name = MakeName(
                firstLongArgument,
                secondLongArgument,
                thirdLongArgument),
            .description = firstLongValue +
                secondLongValue + thirdLongValue +
                fourthLongValue,
        },
        .typed = Child{
            .value = unknown<External>(
                firstArgument,
                secondArgument),
            .enabled = true,
        },
        .count = 3,
    };
    auto array = Array{
        [2] = {
            .value = unknown<External>(
                firstArgument,
                secondArgument),
            .enabled = true,
        },
    };
    Select<
        Box<
            int>,
        Second>(firstArgument, secondArgument);
    Select<
        Box<
            int>>(firstArgument, secondArgument);
    Select<
        Box<
            Box<
                int>>>(firstArgument, secondArgument);
    object.Select<
        Box<
            int>,
        Second>(firstArgument, secondArgument);
    ns::Select<
        Box<
            int>,
        Second>(firstArgument, secondArgument);
    unknown<External<int>, Second>(
        firstArgument,
        secondArgument);
    auto comparison = left < right > tail;
}
