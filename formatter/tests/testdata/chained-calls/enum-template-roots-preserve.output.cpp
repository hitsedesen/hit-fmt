void Build() {
    auto first = MakeBuilder().Item("key").Value(1).Item("other").Value(2).Finish();
    auto second = MakeBuilder<EMode::Map>(EEncoding::Binary).Item("key").Value(1).Item("other").Value(2).Finish();
}

void Variations() {
    auto typed = MakeBuilder<int>(encoding).Item("key").Finish();
    auto qualified = ns::Create<::modes::Mode::Map>(encoding).Item("key").Finish();
    auto number = Create<42>(encoding).Item("key").Finish();
    auto negative = Create<-2>(encoding).Item("key").Finish();
    auto boolean = Create<true>(encoding).Item("key").Finish();
    auto mixed = Create<Unknown, modes::Map>(encoding).Item("key").Finish();
    auto address = Create<&ns::value>(encoding).Item("key").Finish();
    auto member = object.Create<modes::Map>(encoding).Item("key").Finish();
    auto argument = Consume(Create<modes::Map>(encoding).Item("key").Finish(), other);
    auto comparison = left < modes::Map > (encoding).Item("key").Finish();
    auto literalComparison = left < 42 > (encoding).Item("key").Finish();
    auto compactCallComparison = left<modes::Map>(encoding).Item("key").Finish();
    auto spaced = Create <modes::Map>(encoding).Item("key").Finish();
    auto commented = Create< /* mode */ modes::Map>(encoding).Item("key").Finish();
    auto multiline = Create<
        modes::Map>(encoding).Item("key").Finish();
    auto malformed = Create<modes::Map,>(encoding).Item("key").Finish();
    // hit-fmt off
    auto disabled = Create<modes::Map>(encoding).Item("key").Finish();
    // hit-fmt on
    auto compactComparison = left< modes::Map> right;
    auto compactLiteralComparison = left< 42> right;
    auto unresolved = Create<value>(encoding).Item("key").Finish();
    auto relationalArgument = Create< modes::Map < limit>(encoding).Item("key").Finish();
}

void ProtectedRoots() {
    auto directive = Create<
#if USE_MAP
        modes::Map
#else
        modes::Other
#endif
    >(encoding).Item("key").Finish();
}
