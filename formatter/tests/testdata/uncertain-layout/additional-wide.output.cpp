struct Worker: public Base {
    Worker()
        : Base()
        , Handler(Create<External>(std::move(value), TRACE(&Pool::Release, Copy(this)), Options::Timeout(Settings::Limit)))
    { }

    Worker(int)
        : Base()
        , Handler(Create<External>(std::move(value), TRACE(&Pool::Release, Copy(this)), Options::Timeout(Settings::Limit)))
    { }
};

void Owner<First, Second>::Run()
{ }

int Invoke()
{
    return Choose<External<int>, int>({}, 1);
}

Catalog::Catalog()
    : Entries{
        .Enabled = true,
        .Visible = true,
        .Name = "",
    }
{ }

int owner = 1, first = 2, second = 3;
bool comparison = (owner<first,second>::Run());
void Predicate<1 < 2,3>::Run()
{ }
void Predicate<(1 < 2), 3>::Run()
{ }
