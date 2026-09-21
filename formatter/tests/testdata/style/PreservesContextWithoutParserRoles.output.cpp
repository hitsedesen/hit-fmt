void Run()
{
    auto& [key, value] = entries.front();
    auto item = Item{.Index = -1};
    Use(value /*name*/);
    if (first &&
        second)
    {
        item = first
            ? MakeFirst()
            : MakeSecond();
    }
    for (int index = 0;
        index < 10;
        ++index)
    {
        item->Update();
    }
    for (int retry = 0; retry <= 0;) {
        Poll();
    }
    for (;;) {
        Wait();
    }
}

class Derived
    : public Base<FirstType,
        SecondType>
    , public Other
{ };
