void EmitRecords()
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("routes").DoMapFor(
                routesWithMetrics,
                [] (auto fluent, const auto& routeWithMetrics) {
                    auto [route, metrics] = routeWithMetrics;
                    fluent
                        .Item("key").Value(route.GetKey());
                })
        .EndMap();
}
