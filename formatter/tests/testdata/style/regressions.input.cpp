template <class TValue>
int Measure()
{
    constexpr int BitCount = sizeof(TValue) * 8;
    auto mask = (TValue(1) << BitCount) - 1;
    auto growth = sizeof(Header) - sizeof(FixedHeader);
    Store((NativeWord) &result);
    return ++InvocationIndex;
}

void EmitReport()
{
    Format(
        builder,
        pattern,
        first,
        second);
    LOG_DEBUG(
        Logger,
        "Value: %v",
        value);
    CHECK_RESULT(
        IsReady(value))
        << "Value is not ready: " << value;
}

template <class TValue>
void Validate()
{
    static_assert(
        std::derived_from<TValue, CacheValue<Key, Value, Hash>>,
        "Value must derive from the cache base");
}

inline View::View(int value)
    // The comment documents the initializer invariant.
    : Value_(value)
{ }

bool Equal(const Record& lhs, const Record& rhs)
{
    return
        lhs.First == rhs.First && // Fast path.
        lhs.Second == rhs.Second &&
        lhs.Third == rhs.Third;
}

void RegisterCallback()
{
    auto callback = [&] (
        int first,
        int second)
    {
        Use(first, second);
    };
}

class Service
{
public:
    Service(bool enabled)
        : Config_{
            .Enabled = enabled,
            .Name = "service",
        }
    { }

    void Run()
    { }

private:
    struct Config
    {
        bool Enabled;
        const char* Name;
    };

    Config Config_;
};

void ContinueAtFileScope()
{ }

class Dispatcher
{
private:
    void Dispatch(int value)
    {
        switch (value) {
            #define HANDLE(number) \
                case number: \
                    return

            HANDLE(1)
            HANDLE(2)

            #undef HANDLE

            default:
                return;
        }
    }

    void ContinueInsideClass()
    { }
};

template void Consume(
    const Vector<Value>& value);


bool FirstDeclaration();


bool SecondDeclaration();

void CorrectParentIndentation()
{
    auto result = Wrap(
            object
        ->Method());
}

void KeepIntentionalBlankLine()
{

    Work();
}

namespace Outer {
namespace Inner {

void Empty()
{ }

} // namespace Inner
} // namespace Outer

constexpr int PackedTable[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
    72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
    84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107,
    108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131,
    132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143
};
