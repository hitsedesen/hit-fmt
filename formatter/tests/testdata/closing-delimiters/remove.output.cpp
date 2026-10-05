void Example()
{
    auto item = Item{.label = "draft"};
    auto values = {1, 2};
    auto compact = Item{.label = "ready"};
    auto plain = Item{.label = "final"};
    auto annotated = Item{.label = "note" // a comment owns this line
    };
}
