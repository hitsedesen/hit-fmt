void Example()
{
    auto options = {.Node = {.Identifier = "identifier", .Inputs = {1}}, .Name = "Example"};
    auto empty = {.Value = {}, .Other = Record{3}, .Count = value = 7};
    auto commented = {.First/*before*/ = /*after*/3, .Second = /*value*/ 4};
    int slots[4] = {[1] = 7, [2] = 8};
    auto ordinary = {value = 7, 8};
}
