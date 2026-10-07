void Configure(
    int count = DefaultElementCountForCurrentProcessingStage,
    int other = 0);

void Short(int count = 0);

struct Options
{
    void Configure(
        int count = DefaultElementCountForCurrentProcessingStage,
        int other = 0);
};

void Evaluate(
    int value = ComputeDefaultValueForCurrentStage(
        firstValue,
        secondValue),
    int other = 0);

void Combine(
    int value = firstLongOperandForDefaultValue + secondLongOperandForDefaultValue);

void Existing(
    int count =
        DefaultElementCountForCurrentProcessingStage,
    int other = 0);

auto configure = [] (
        int count = DefaultElementCountForCurrentProcessingStage,
        int other = 0) { return count; };
