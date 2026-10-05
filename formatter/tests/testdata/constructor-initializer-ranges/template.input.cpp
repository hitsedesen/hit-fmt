struct Pending {
    Pending() : State<void>(producerReferences, consumerReferences, cancellationReferences, Error(static_cast<const Error&>(value))), result(std::move(value)) {}
    Pending(int tag) : State<7>(producerReferences, consumerReferences, cancellationReferences, Error(static_cast<const Error&>(value))) {}
    Pending(short tag) : detail::State<Item<Key, Value>>(producerReferences, consumerReferences, cancellationReferences, Error(static_cast<const Error&>(value))) {}
    Pending(long tag) : Envelope<Key, Value>::State<7>(first, second) {}
    Pending(char tag) : State<void>{producerReferences, consumerReferences, cancellationReferences, Error(static_cast<const Error&>(value))}, result{1} {}
    Pending(bool tag) : State<void>(1, 2), State<7>{3, 4} {}
    Pending(float tag) : State<void>(assemble(producerReferences, consumerReferences, cancellationReferences, Error(static_cast<const Error&>(value)))) {}
    Pending(double tag) : State<FirstVeryLongTemplateArgumentType, SecondVeryLongTemplateArgumentType, ThirdVeryLongTemplateArgumentType>(1, 2) {}
    Pending(unsigned tag) : State<7>(producerReferences, consumerReferences, cancellationReferences, Error(static_cast<const Error&>(value)))... {}
};
