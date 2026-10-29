void Report() {
  message(value,
    alpha,
    beta,
    gamma);
  message(42,
    alpha,
    beta,
    gamma);
  message(true,
    alpha,
    beta,
    gamma);
  message(nullptr,
    alpha,
    beta,
    gamma);
  message(-value,
    alpha,
    beta,
    gamma);
  message(value + offset,
    alpha,
    beta,
    gamma);
  message("literal" + suffix,
    alpha,
    beta,
    gamma);
  message(ready ? left : right,
    alpha,
    beta,
    gamma);
  message(inner(value, offset),
    alpha,
    beta,
    gamma);
  message(
    leftArgument +
    middleArgument +
    rightArgument,
    alpha,
    beta);
  message(
    inner(
      leftArgument,
      middleArgument,
      rightArgument),
    alpha,
    beta);
  message({left, right},
    alpha,
    beta,
    gamma);
  message({leftArgument,
    middleArgument,
    rightArgument},
    alpha,
    beta);
  message([](int value) {
    return value + 1;
  },
    alpha,
    beta);
  message(
    firstArgumentWithAnIntentionallyLongName,
    alpha,
    beta);
  message(
    firstArgumentWithAnIntentionallyLongName);
  receiver
    .message(value + offset,
      alpha,
      beta,
      gamma);
  message("left" " right",
    alpha,
    beta,
    gamma);
  message("first piece "
    "second piece");
  message(inner(
    leftArgument,
    middleArgument,
    rightArgument));
  message(
    /* prefix */ value + offset,
    alpha,
    beta);
  message(value /* suffix */,
    alpha,
    beta,
    gamma);
  message(value, // note
    alpha,
    beta,
    gamma);
  message( // prefix
    value,
    alpha,
    beta,
    gamma);
  message(value + offset,
    alpha,
    beta,
    gamma);
  other(
    value + offset,
    alpha,
    beta,
    gamma);
  message_with_suffix(
    value,
    alpha,
    beta,
    gamma);
  message();
// clang-format off
message(  value + offset,alpha,beta,gamma);
// clang-format on
}
void message(int value,
  int alpha, int beta,
  int gamma);
