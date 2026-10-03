void Example(int first, int second)
{
    Use(first, second);
    auto values = {first, second};
    auto object = ObjectType{first, second};
    if (first) {
        Use(second);
    }
}
