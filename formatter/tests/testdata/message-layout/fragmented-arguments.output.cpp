void F()
{
    THROW_ERROR_EXCEPTION("A deliberately long message already split into adjacent literals "
        "to keep its first fragment on the macro line without exceeding the configured width");
    THROW_ERROR_EXCEPTION("A deliberately long message already split into adjacent literals "
        "to keep its first fragment on the macro line without exceeding the configured width");
    THROW_ERROR_EXCEPTION(u8"A deliberately long message already split into adjacent literals "
        u8"to keep its first fragment on the macro line without exceeding the configured width");
    THROW_ERROR_EXCEPTION(R"(A deliberately long message already split into adjacent literals )"
        "to keep its first fragment on the macro line without exceeding the configured width");
    THROW_ERROR_EXCEPTION("An extended error description that exceeds the column limit once all fragments are combined "
        "with this additional context",
        first);
    THROW_ERROR_EXCEPTION(
        "An extended error description that exceeds the column limit once all fragments are combined !"
        "with this additional context",
        first);
}
