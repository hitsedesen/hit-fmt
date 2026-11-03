void Example() {
    CheckEqual(
        result.message,
        kExampleUnavailableReasonPrefix +
            ConvertToText(example_models::ExampleUnavailableReasonType::kExampleUnavailableForSelectedPaymentMethod) + "suffix"
    );
}
