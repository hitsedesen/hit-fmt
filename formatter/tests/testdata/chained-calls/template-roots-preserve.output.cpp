void Templates() {
    factory<int>().First().Second();
    factory<box<box<int>>>().First().Second();
    factory<(1 < 2), (8 >> 1)>().First().Second();
    factory<decltype([] {
        return 1;
    })>().First().Second();
    factory<int>()(value).Finish();
    ns::Builder<int>::Make<double>().First().Second();
    ns::Builder<box<int>>::Make<double>().First().Second();
    ns::Builder<int>::template Make<double>().First().Second();
    ns /* qualifier */ :: Builder<int>::Make<double>().First().Second();
    other::Builder<int>::Make<double>().First().Second();
    Builder<int>::Make<double>().First().Second();
    factory_extra<int>().First().Second();
    factory<int>().Field;
    factory<int>().Only();
    factory<value>(argument).First().Second();
    ns::Builder<value>::Make<double>().First().Second();
    factory < value > argument.First().Second();
    // hit-fmt off
    factory<int>().First().Second();
    // hit-fmt on
}
