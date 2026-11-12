auto x = Make({firstValue, secondValue}) +
    trailingValue;
auto y = Make({.first = one, .second = two}) +
    trailingValue;
auto z = Make({firstValue, secondValue,
    thirdValue}) + trailingValue;
Data data{.firstField = firstValue,
    .secondField = secondValue};
int indexed[] = {[0] = firstValue,
    [1] = secondValue};
Data preserved{firstValue, secondValue};
