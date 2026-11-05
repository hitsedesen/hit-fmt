#include <string>
#include <vector>

void Example() {
    std::vector<std::string> values{
        "alpha",
        "bravo",
        "charlie",
        "delta",
        "echo",
    };
}

void Nearby() {
    Box<Item> flat{
        "alpha",
        "bravo",
        "charlie",
        "delta",
        "echo"
    };
    Box<Item> assigned = {
        "alpha",
        "bravo",
        "charlie",
        "delta",
        "echo"
    };
    Box<Item> nested{
        {
            "alpha",
            "bravo",
            "charlie",
            "delta"
        },
        {"echo", "foxtrot"}
    };
    Box<Item> calls{
        Build(firstLongValue,
            secondLongValue),
        "tail"
    };
    Box<Item> designated{
        .first = 1,
        .second = 2,
        .third = 3
    };
    Box<Item> comment{
        "alpha",
        "bravo",
        "charlie",
        "delta",
        "echo"
    } /* end */;
    Box<Item> empty{};
    Box<Item> exact{"1234567890123456"};
    Box<Item> extra{
        "12345678901234567"
    };
    int array[] = {1, 2, 3};
}
