namespace sample::formatting {

class Item
{
public:
    void Empty()
    { }

    void Run()
    {
        if (Ready()) {
            Work();
        } else {
            Stop();
        }
    }
};

void LongControl()
{
    if (FirstCondition() &&
        SecondCondition())
    {
        Work();
    }
}

void OtherControls()
{
    do {
        Work();
    } while (Ready());
    try {
        Work();
    } catch (...) {
        Stop();
    }
}

auto Callback = [] (int value) {
    return value;
};

} // namespace sample::formatting
