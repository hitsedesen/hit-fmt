struct Item
{
    int Value;
};

template <auto... Callbacks>
struct Actions
{ };

template <class T>
struct Box
{ };

template <auto Action>
constexpr int Invoke()
{
    return Action();
}

using Accessors = Actions<
    [] (const Item* object) {
        return object->Value;
    },
    [] {
        return 2;
    },
    [] <class Left, class Right> (Left left, Right right) noexcept -> int {
        if (left < right) {
            return left;
        }

        return right;
    }>;

using Nested = Box<Actions<
    +[] (const Item* object) {
        return object->Value;
    },
    ([] {
        return [] {
            return 3;
        } ();
    })>>;

int Compute()
{
    return Invoke<[] {
        return 4;
    }>();
}

template <class Signature, class Array, int Value>
struct Mixed
{ };

using Types = Mixed<int (*)(int), int[2], (8 >> 1)>;

int Read(const Item* objects)
{
    return objects[0].Value;
}
