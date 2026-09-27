void Example(
    int first,
    int second)
{
    Use(
        first,
        second);
    auto values = {
        first,
        second};
    auto object = ObjectType{
        first,
        second};
    if (first) {
        Use(second);
    }
    auto lambda = [] {
        return first;
    };
    Use(
        first, // keep this comment
        second // keep this one too
    );
}
