auto c = MakeServiceClient()
         .Configure(mode)
         .Start();
auto expanded =
    MakeClient()
        .Configure(firstMode)
        .Select(secondMode)
        .Start();
auto fields =
    MakeServiceClient()
        ->first.second->third;
auto invoked = MakeClient()
               .Configure(mode)
               (first)
               (second)
               .Start();
void Run() {
    ProcessPendingTasks(
                        first,
                        second
    );
    Use(
        first,
        second,
        third,
        fourth,
        fifth
    );
    Use(
        first,
        /* next */ second,
        third,
        fourth
    );
    Use(
        first, // attached
        second,
        third
    );
    Use(
        Inner(
              first,
              second,
              third
        ),
        last
    );
    Use(
        [] {
            Step();
            Step();
        },
        first,
        second
    );
    Use();
    Use(single);
// hit-fmt off
Use(  untouched,   arguments  );
// hit-fmt on
}
void LongFunctionName(
                      int first,
    int second,
                      int third
);
template<
         class First,
         class Second,
         class Third
> struct Record
    : First, Second, Third {
};
Record<
       First, Second, Third
> record;
Outer<
      Inner<First, Second>,
      Third
> nested;
auto values = {
               first,
               second,
               third,
               fourth,
};
auto compactValues =
    {first, second};
auto commentedValues = {
                        first,
    second, /* trailing */};
auto capture = [
                first,
                second,
                third,
                fourth
] {
    Use(first, second);
};
struct Item
    : First, Second, Third {
    Item()
        : first(1)
          , second(2)
          , third(3) {
    }
};
