namespace Demo {
struct Base {
};
struct Derived: Base {
  int value;
  Derived(
    int first,
    int second
  ): value(first + second)
  {
  }
  int Compute(
    int first,
    int second
  )
  {
    for (
      int index = 0;
      index < first;
      ++index
    )
    {
      auto callback = [
        first,
        second
      ](int extra)
      {
        return first
          ? second + extra : 0;
      };
      consume(
        first,
        callback(second),
        factory()(
          first
        ).finish()
      );
    }
    return value;
  }
};
}
