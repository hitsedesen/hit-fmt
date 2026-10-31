void Construct()
{
    Prepare();
    Box<T>(value);
    Prepare();
    auto item = Box<T>(value);
    Prepare();
    Consume(storage::Box<T>(value));
    Prepare();
    Box<T>{value};
    Prepare();
    Box<int>(value);
    Prepare();
    less < middle > (value);
    Prepare();
    unknown  *  pointer;
    Prepare();
    Assign()  =  value ?: fallback;
    Finish();
}
