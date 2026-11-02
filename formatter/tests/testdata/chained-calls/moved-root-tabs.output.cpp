void Example()
{
    UnknownRoot()
	.First()
	    .Second().Finish();
    UnknownRoot()
	->First()
	    ->Finish();
    auto exceptionallyLongResultName = UnknownRoot()
	.First()
	    .Finish();
    MAKE_TASK()
	.First().Finish();
    Consume(UnknownRoot()
	.First()
	    .Finish());
    UnknownRoot()
	.First()
	    .Finish();
    object.member;
}

namespace Outer {
    namespace Inner {
	void Configure()
	{
	    auto operation = service->Execute(RequestPlan()
		.Read<Entry>(source)
		.Write<Entry>(destination)
		.Attempts(0), MakeShared<Handler>(), Options().Wait(false));
	    if (ready) {
		UnknownRoot()
		    ->Read<Entry>(source)
			->Finish();
	    }
	}
    }
}
