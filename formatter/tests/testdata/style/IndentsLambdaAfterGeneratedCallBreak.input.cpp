void Run()
{
    auto guard = Finally([expectedRowSize, cumulativeDataWeightColumnId, timestampColumnId] (const DescriptiveInputTypeWithAnIntentionallyLongName& input) {
        Subtract(expectedRowSize, cumulativeDataWeightColumnId, timestampColumnId, input);
    });
}
