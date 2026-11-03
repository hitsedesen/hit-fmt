auto Build(const Settings& settings) {
    if (settings.enabled) {
        if (settings.ready) {
            if (settings.valid) {
                return Result{
                    .configuration_for_requirement_alternatives =
                        processing::BuildConfigurationForRequirementAlternatives(settings),
                    .enabled =
                        true
                };
            }
        }
    }
}

Result Nested() {
    return Result{
        .nested =
            Options{
                .threshold =
                    ComputeThreshold(seed),
                .enabled =
                    true
            },
        .values =
            {
                ComputeValue(seed),
                destination =
                    ComputeValue(seed),
                Options{
                    .enabled =
                        false
                }
            },
        .with_comment /* assignment */ = /* value */ ComputeValue(seed),
        .with_line_comment = // value
            ComputeValue(seed),
        .comparison =
            left == right,
        .combined =
            requirements.primary_setting_is_enabled && requirements.secondary_setting_is_enabled &&
            requirements.fallback_setting_is_enabled
    };
}
int indexed[] =
    {
        [2] =
            ComputeValue(seed),
        [sizeof(index = 2)] =
            7,
        [true ? 4 : 5] =
            8
    };
