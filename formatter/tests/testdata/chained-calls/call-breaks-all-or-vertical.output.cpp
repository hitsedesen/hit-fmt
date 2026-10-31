void Chains()
{
    builder
        .Then();
    builder.Start()
        .Then()
        .Value()
        .Finish();
    pointer->Start()
        ->Then()
        ->Value();
    factory().Start()
        ->Then()
        .Value();
    builder
        .Then<int>().Value();
    pointer
        ->template Then<Box<int>>().Value();
    builder.Item()
        .Then()
        .Value();
    builder.Begin()
        .Item()
        .Then()
        .Value()
        .End()
        .Finish();
    builder.Start() /* next stage */ .Then().Value();
    builder.Start()
        .Then(/* argument */ value)
        .Value();
    builder.Start()
        // next stage
        .Then()
        .Value();
    if (builder
        .Then().Ready()) {
        work();
    }
    builder.With(other
        .Then())
        .Value();
    builder.Then;
    builder.Then.Other();
    builder.ThenExtra().Value();
    builder.then().Value();
    Then().Value();
    ns::Then().Value();
    builder
        .Then()().Value();
    // hit-fmt off
    builder.Then().Value();
    // hit-fmt on
}
