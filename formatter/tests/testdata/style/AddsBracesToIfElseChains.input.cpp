void Run()
{
    if (first)
        One();
    else if (second)
        Two();
    else
        Three();

    if (ready) // explanation
        Work(); // trailing
}

consteval int Evaluate()
{
    if consteval
        return 1;
    else
        return 0;
}
