void choose() {
    auto value =
          LeftCondition() &&
                RightCondition()
          ? left +
                right
          : otherLeft +
                otherRight;

    auto singleCondition =
          condition
          ? firstValue
          : fallbackValue;

    auto inlineValue = condition ? left + right : up + down;
    auto firstLine = condition
          ? left +
                right
          : otherLeft +
                otherRight;

    auto calls =
          condition
          ? outer(inner(
                argument))
          : other(
                otherArgument);

    auto arguments = select(
          condition
          ? first
          : second);

    auto nested =
          firstCondition
          ? firstValue
          : secondCondition
                ? left +
                      right
                : fallback;

    auto body =
          condition
          ? []() {
              work();
          }
          : []() {
              other();
          };
}
