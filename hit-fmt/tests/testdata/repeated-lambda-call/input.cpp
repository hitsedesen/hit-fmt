void Configure()
{
    auto chain = GetChain()
        .OnAdded(MakeHandler([&] (const ComponentPtr& component) -> TBuilder<VeryLongComponentSpecificationTypeWithExtraSuffix> {
            VERIFY_COMPONENT(
                component->Ready,
                "addition");
            ready = true;
            return MakeBuilder(component);
        }))
        .OnRemoved(MakeHandler([] (const ComponentPtr& component) {
            VERIFY_COMPONENT(
                component->Removable,
                "removal");
        }));
}
