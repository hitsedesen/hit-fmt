struct Pending
{
    Pending()
        : State<void>(
            producerReferences,
            consumerReferences,
            cancellationReferences,
            Error(static_cast<const Error&>(value)))
        , result(std::move(value))
    { }

    Pending(int tag)
        : State<7>(
            producerReferences,
            consumerReferences,
            cancellationReferences,
            Error(static_cast<const Error&>(value)))
    { }

    Pending(short tag)
        : detail::State<Item<Key, Value>>(
            producerReferences,
            consumerReferences,
            cancellationReferences,
            Error(static_cast<const Error&>(value)))
    { }

    Pending(long tag)
        : Envelope<Key, Value>::State<7>(first, second)
    { }

    Pending(char tag)
        : State<void>{
            producerReferences,
            consumerReferences,
            cancellationReferences,
            Error(static_cast<const Error&>(value)),
        }
        , result{1}
    { }

    Pending(bool tag)
        : State<void>(1, 2)
        , State<7>{3, 4}
    { }

    Pending(float tag)
        : State<void>(assemble(
            producerReferences,
            consumerReferences,
            cancellationReferences,
            Error(static_cast<const Error&>(value))))
    { }

    Pending(double tag)
        : State<
            FirstVeryLongTemplateArgumentType,
            SecondVeryLongTemplateArgumentType,
            ThirdVeryLongTemplateArgumentType>(1, 2)
    { }

    Pending(unsigned tag)
        : State<7>(
            producerReferences,
            consumerReferences,
            cancellationReferences,
            Error(static_cast<const Error&>(value)))...
    { }
};

Owned::Owned()
    : Value(Build<const Schema::Key>(
        0,
        Set<Text>{
            "the_first_deliberately_long_label_for_an_owned_constructor_argument",
            "the_second_deliberately_long_label_for_an_owned_constructor_argument",
        },
        Helper::Create<Owned>()))
{ }
Owned::Owned(int tag)
    : Value(Build<Unknown>(Set<Text>{"first", "second"}, Helper::Create<Owned>()))
{ }
Owned::Owned(short tag)
    : Value(Build<Unknown>(Helper::Create<Owned>(), Set<Text>{"first", "second"}, tag))
{ }
Owned::Owned(long tag)
    : Value(Build<Unknown>(Set<Text>{"first"}, Set<Text>{"second"}, tag))
{ }
Owned::Owned(char tag)
    : Value(select < left > (right, other))
    , Ready(true)
{ }
void neighboring()
{
    consume(Set<Text>{"first", "second"}, Helper::Create<Owned>());
}
