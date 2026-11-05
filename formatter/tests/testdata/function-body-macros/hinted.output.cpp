using Known = int;
DEFINE_BENCHMARK(TFixture, Run)(TState& state)
{
    Consume(state);
}
static inline DEFINE_CASE(Suite, Curried) /* parameters */ (void (*callback)(int), Known& value) ()
{
    auto sum = left + right;
    callback(value);
}
DEFINE_CASE()()
{
    if (ready) {
        Use(value);
    }
}
DEFINE_CASE( Known , Nested(/* argument */ 1+2,(3+4)))
{
    auto total = ReadPrimaryAmount() + ReadSecondaryAmount() +
        ReadFallbackAmount();
    if (total > 0) {
        Send(total, firstArgument, secondArgument,
            thirdArgument);
    }
    TOKEN_TEXT(alpha   +  beta);
}
DEFINE_CASE()
{
    Opaque value;
    auto count = GetCount() + GetExtraCount();
    Use(count);
}
struct Holder
{
    DEFINE_METHOD( Holder,   Member ) (int Known)
    {
        Known* pointer;
        auto count = FetchFirstCount() + FetchSecondCount();
        return count;
    }
};
UNLISTED_CASE( Suite , Other ) {untouched=left+right;}
DEFINE_CASE(Suite, DeclarationOnly);
DEFINE_CASE (Suite, DeclarationOnly)(Known value);
static DEFINE_CASE(Suite, const Known& value)
{
    auto sum = left + right;
    Use(sum);
}
static inline DEFINE_CASE(Suite, Helper(alpha   +  beta))
{
    Use(value);
}
static DEFINE_CASE(Suite, DeclarationOnly);

DEFINE_METHOD( Suite , Nested(1+2))
{
    DEFINE_CASE( First , Example ) /* parameters */ (Known& value)
    {
        ProcessCollectedMeasurements(primaryMeasurementSequence,
            secondaryMeasurementSequence,
            fallbackMeasurementSequence,
            expectedMeasurementSequence,
            measurementComparisonPolicy, messageOutput);
        if (ready) {
            Send(value);
        }
        TOKEN_TEXT(alpha   +  beta);
    }
    DEFINE_METHOD(NestedSuite)
    {
        DEFINE_CASE(Second)
        {
            auto count = left + right;
            Use(count);
        }
    }
    DEFINE_CASE(DeclarationOnly);
    auto result = DEFINE_CASE(value);
    auto curried = DEFINE_CASE(value)(other);
    UNLISTED_CASE( Unknown , Example ) {untouched=left+right;}
}
namespace Comparison {
    DEFINE_CASE(OrdinaryScope)
    {
        ProcessCollectedMeasurements(primaryMeasurementSequence,
            secondaryMeasurementSequence,
            fallbackMeasurementSequence,
            expectedMeasurementSequence,
            measurementComparisonPolicy, messageOutput);
    }
}
