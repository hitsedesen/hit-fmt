void RegisterHandler()
{
    handler = Wrap([] (int value)
        -> Result {
            return MakeResult(value);
        });
}
