void function()
{
    union {
        Value value;
        char bytes[sizeof(Value)];
    } storage;

    struct {
        int count;
    } state;

    struct Named
    {
        int count;
    }
    named;
}
