void f()
{
    auto grouped = (FirstCondition || SecondCondition || ThirdCondition) << "extra context";
    auto indexed = values[FirstCondition || SecondCondition || ThirdCondition] << "extra context";
    auto deepGrouped = (LongConditionFromPrimarySource || LongConditionFromSecondarySource || LongConditionFromTertiarySource) << "context";
    auto deepIndexed = values[LongConditionFromPrimarySource || LongConditionFromSecondarySource || LongConditionFromTertiarySource] << "context";
}
