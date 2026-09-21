namespace sample::formatting {

void Run()
{
    VeryLongFunctionName(
        veryLongFirstArgumentName,
        veryLongSecondArgumentName,
        veryLongThirdArgumentName,
        veryLongFourthArgumentName);
    Use(
        first,
        second);
    Await(CALLBACK(&Worker::Run, this)
        .Via(Executor_)
        .Invoke())
        .Check();
    Await()
        .Apply(CALLBACK([] (const Failure& error) {
            Handle(error);
        }))
        .Via(Executor_);
    Use({
        Build()
            .SetValue(first)
        ,
        Build()
            .SetValue(second)
        ,
    });
    for (int index = 0;
        index < Count && IsValid(index);
        ++index)
    {
        Process(index);
    }
}

} // namespace sample::formatting
