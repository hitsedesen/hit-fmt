typedef void Handler(bool, int*, char*);
void visit() {
  for (const Cell<int>* first = left->front(), *second = right->front();
       first != nullptr; first = first->next()) {
    auto value = *second;
    auto maker = &factory;
  }
}
struct LocalTable {
};
void visit_sources(int width, int height, int* scale) {
  for (Table* source = Acquire(
    width * height); source != nullptr; source = source->next()) {
    Consume(source);
  }
  for (::storage::Row* source = Acquire(
    width * height); source != nullptr; source = source->next()) {
    Consume(source);
  }
  for (Table** source = Acquire(
    width * height); source != nullptr; source = Next(source)) {
    Consume(source);
  }
  for (Table const* const source = Acquire(
    width * height); Ready(source); Advance()) {
    Consume(source);
  }
  for (ExternalValue& value = Current(
    width * height); Ready(value); Advance(value)) {
    Consume(value);
  }
  for (ExternalValue&& value = Current(
    width * height); Ready(value); Advance(value)) {
    Consume(value);
  }
  for (Opaque *  source = Acquire(
    width * height); source != nullptr; source = source->next()) {
    Consume(source);
  }
  for (LocalTable* source = Acquire(
    width * height); source != nullptr; source = Next(source)) {
    Consume(source);
  }
  for (width * *scale; width * height < 100; width = width * height) {
    Consume(width * height);
  }
  for (unknown*operand; Ready(); Advance()) {
    Consume();
  }
  for (unknown * 2; Ready(); Advance()) {
    Consume();
  }
}
REGISTER_CASE(Label, Scenario, Make(1), &callback);
REGISTER_CASE(alpha, , omega);
REGISTER_CASE(Label, Scenario, Make(1), [](const Item& item) {
  return item.value;
});
static_assert(!EMPTY_MARKER(, ), "");
static_assert(!EMPTY_MARKER(alpha, , omega), "");
static_assert(!EMPTY_MARKER(alpha, ), "");
