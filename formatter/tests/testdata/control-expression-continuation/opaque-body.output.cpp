Options Run()
{
    return Factory::Make(
        {
            .Node = {.Identifier = "a_deliberately_long_identifier_for_a_complete_initializer", .Inputs = {1}},
            .Name = "Example",
        },
        [] (Options* value) { if (Factory::Control* current = Factory::Find(value)) {
                value->SetCurrent(foo);
            } });
}

void Inspect()
{
    Dispatch(
        {.Name = "nested", .Values = {1, 2}},
        Wrap([] (Options* value) {
            if (external::Handle  *  pointer =
                Acquire(first_argument_for_the_shared_assignment, second_argument_for_the_shared_assignment))
            {
                value->SetCurrent(pointer);
            }
        }),
        {.Name = "tail"});
    Dispatch(
        [] (Options* value) { while (ExternalCount& current = Acquire(first, second)) {
                value->SetCurrent(current);
            } },
        {.Name = "after_lambda", .Values = {1, 2}});
    Factory::Make(
        {
            .Name = "protected_argument",
        }, YT_DEFINE_ERROR_ENUM((Opaque, 1)), [] (Options* value) { if (external::Handle* current = Acquire(value)) {
            value->SetCurrent(current);
        } });
}
