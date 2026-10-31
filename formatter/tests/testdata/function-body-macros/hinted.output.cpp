using Known = int;
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
    DEFINE_METHOD( Holder,   Member )
    {
        auto count = FetchFirstCount() + FetchSecondCount();
        return count;
    }
};
UNLISTED_CASE( Suite , Other ) {untouched=left+right;}
DEFINE_CASE(Suite, DeclarationOnly);
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
