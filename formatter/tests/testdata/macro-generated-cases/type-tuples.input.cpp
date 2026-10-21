class TMock
{
public:
    MOCK_METHOD(
        void,
        DoFooBarium, (
        const std::vector<VeryVeryVeryVeryLongClassName*>&, ::Foo::Barium*, duration, ui64
        ),
        (override));
    DECLARE_SIGNATURE(void, Other, (
        const std::pair<int, long>&,
        ::Other::Value*,
        duration));
    DECLARE_SIGNATURE(void, Commented, (
        const int*,
        ::Other::Value*) /* argument note */);
    DECLARE_SIGNATURE(void, Wide, (
        const std::vector<VeryVeryVeryVeryLongClassName*>&,
        const std::vector<AnotherVeryVeryVeryLongClassName*>&,
        ::Other::Value*));
    // hit-fmt off
    DECLARE_SIGNATURE(void, Frozen, (
    const int*, long
    ));
    // hit-fmt on
};

void Expressions()
{
    Consume((left, right));
    Consume((left * right, other & value));
    Consume((Build(), Next()));
}
