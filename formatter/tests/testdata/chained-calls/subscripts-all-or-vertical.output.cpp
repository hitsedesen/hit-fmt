void Controls(Value& data) {
    data["short"]["path"] = 42;
    data["configuration"]
        ["primary_group"]
        ["additional_configuration"]
        ["calculation_results"]
        ["metadata"]
        ["user_settings"]
        ["displayed_price"] = 42;
    data.Load()
        ["configuration"]
        ["primary_group"]
        .Get()
        ["additional_configuration"]
        ["calculation_results"]
        .Finish()
        ["displayed_price"] = 42;
    Fetch()["configuration"]
        ["primary_group"]
        ["additional_configuration"]
        ["calculation_results"]
        ["metadata"]
        ["user_settings"]
        ["displayed_price"] = 42;
    Consume(
        data["configuration"]
            ["primary_group"]
            ["additional_configuration"]
            ["calculation_results"]
            ["metadata"]
            ["user_settings"]
            ["displayed_price"]
    );
    data[indices
        ["configuration"]
        ["primary_group"]
        ["additional_configuration"]
        ["calculation_results"]
        ["metadata"]
        ["user_settings"]
        ["displayed_price"]]
        ["tail"] = 42;
    data[key]().Open();
    data[key]()(argument);
    data["left"] /* next dimension */ ["right"] = 42;
    data[ /* index */ first_index]["tail"] = 42;
    int array[first_dimension][second_dimension];
    auto lambda = [first_index, second_index] {
        return first_index + second_index;
    };
    data[first_index]["tail"] = 42;
    data[first_index + second_index]["tail"] = 42;
    data[first_descriptive_configuration_index + second_descriptive_configuration_index +
        third_descriptive_configuration_index]
        ["tail"] = 42;
    data[Select(first_index, second_index)]["tail"] = 42;
    data
        ["single_indivisible_string_literal_that_is_longer_than_the_configured_column_limit_and_must_never_be_split_by_subscript_chain_wrapping"] =
        42;
}
