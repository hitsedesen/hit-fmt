template<class A, class B> struct Pair {
};
template<class T> struct Box {
};

storage::Map<FirstLongTemplateArgument,
    SecondLongTemplateArgument,
    ThirdLongTemplateArgument> flat;
storage::Map<types::Box<FirstLongTemplateArgument>,
    types::Pair<SecondLongTemplateArgument,
        ThirdLongTemplateArgument>> nested;
storage::Outer<storage::Middle<storage::Inner<
    FirstLongTemplateArgument,
    SecondLongTemplateArgument>>> deep;
storage::Map<types::Box<FirstLongTemplateArgument>,
    types::Pair<SecondLongTemplateArgument,
        ThirdLongTemplateArgument> > spaced;
Pair<> empty;
Fixed<(1 << 2), (bits >> 1), (left < right > tail),
    7> expressions;
Pair<FirstLongTemplateArgument,
    SecondLongTemplateArgument> authored;

template<class T> void UseArguments(T value) {
    Invoke<T, types::Box<FirstLongTemplateArgument>,
        SecondLongTemplateArgument>(value);
    auto result = object.template Convert<
        FirstLongTemplateArgument,
        SecondLongTemplateArgument>(value);
    auto cast = static_cast<types::Pair<
        FirstLongTemplateArgument,
        SecondLongTemplateArgument>>(value);
    auto qualified =
        static_cast<const UnknownType*>(pointer);
    if constexpr (traits::same<T, types::Box<
        FirstLongTemplateArgument>> && MoreChecks()) {
        Use(value);
    }
    auto comparison = left < right > ::Result();
    auto qualifiedComparison =
        Box<T>::value < right > ::Result();
    auto shift = value >> 2;
    VERBATIM(types::Pair< FirstLongTemplateArgument ,  SecondLongTemplateArgument >);
    // hit-fmt off
    Pair< FirstLongTemplateArgument ,  SecondLongTemplateArgument > disabled;
    // hit-fmt on
}

Pair<FirstLongTemplateArgument,,SecondLongTemplateArgument> malformed;
Pair<FirstLongTemplateArgument,> trailing;
