void f()
{
    Send(
        firstArgument,
        secondArgumentWithLongerName,
        thirdArgumentWithLongerName);
    Send(firstArgument, secondArgument);
    BuildSystemEventProvider(
        /*enabled*/ true,
        /*family*/ EventFamily::PlainText);
    Send(
        /* first */ /* label */ firstArgument,
        secondArgumentWithLongerName);
    Send(/* first */ firstArgument, secondArgument);
}
