void function()
{
    static_assert(
        offsetof(library::records::StoredValue, length) == 4 &&
        sizeof(library::records::StoredValue::length) == 4);
}
