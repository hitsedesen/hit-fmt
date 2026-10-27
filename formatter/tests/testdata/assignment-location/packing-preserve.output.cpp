bool Inspect()
{
    auto ready = first_long_predicate() || second_long_predicate() || third_long_predicate();
    ready = first_long_predicate() || second_long_predicate() || third_long_predicate();
    ready |= first_long_predicate() || second_long_predicate() || third_long_predicate();
    auto small = 1;
    destination_with_an_extended_name = first_long_predicate() || ready;
    left = middle = first_long_predicate() || second_long_predicate() || third_long_predicate();
    auto worker = [](int x) {
        return x;
    };
    auto matched = Check(
        "first_long_pattern") || Check(
            "second_long_pattern") || Check([](int x) {
                return x;
            });
    return Check(
        "first_long_pattern") || Check(
            "second_long_pattern") || Check([](int x) {
                return x;
            });
}
enum Flags {
    Selected = first_long_predicate() || second_long_predicate() || third_long_predicate() };
void Consume(
    bool ready = first_long_predicate() || second_long_predicate() || third_long_predicate());
