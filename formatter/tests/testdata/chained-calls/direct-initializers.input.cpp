void Register(TRegistrar registrar)
{
    registrar.Postprocessor([] (TThis* spec) {
        struct tm parsed
        { };
        int empty
        { };
        TValue value
        {0};
        TValue continued
            {1};
        TValue commented
        // Keep the initializer on its own line.
        {2};
        TValue multiline
        {
            3,
            4
        };
        TValue attached{5};
        TValue assigned =
        {6};
        Use(parsed, empty, value, continued, commented, multiline, attached, assigned);
    });
}
