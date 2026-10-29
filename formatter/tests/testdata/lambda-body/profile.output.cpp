void Example()
{
    auto compact = [] { };
    auto padded = [] { };
    auto tight = [] {
        return 1;
    };
    auto spaced = [] {
        return 2;
    };
    auto multiline = [] {
        return 3;
    };
    auto tooLong = [a = MakeLongCaptureValue()] {
        return VeryLongFunctionNameWithArguments(a);
    };
}
