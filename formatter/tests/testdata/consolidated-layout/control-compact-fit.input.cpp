template<class T>
void Visit(T value) {
    if constexpr (
                  std::same_as<T, Text>) {
        ProcessFirstStatement(value);
        ProcessSecondStatement(value);
        ProcessThirdStatement(value);
    } else if constexpr (std::same_as<T,
                                      Image>) {
        ProcessFirstStatement(value);
        ProcessSecondStatement(value);
        ProcessThirdStatement(value);
    }
}

void Poll() {
    if (Ready()) {
        ProcessFirstStatement();
        ProcessSecondStatement();
        ProcessThirdStatement();
    }
    while (Ready()) {
        ProcessFirstStatement();
        ProcessSecondStatement();
        ProcessThirdStatement();
    }
    for (auto& item : items) {
        ProcessFirstStatement(item);
        ProcessSecondStatement(item);
        ProcessThirdStatement(item);
    }
    if (FirstConditionWithAVeryLongName() && SecondConditionWithAVeryLongName()) {
        Work();
    }
    if (Ready()) { Work(); }
}
