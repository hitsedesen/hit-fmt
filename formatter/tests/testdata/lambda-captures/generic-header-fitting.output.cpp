void SelectMember()
{
    const auto fillSelectedMember = [&] <class T> (int selected, T Transformation::ValueStorage::* field) { };
    const auto fillValue = [=] <typename Value> (Value value, VeryLongParameterType additionalArgument) { };
    const auto shortLambda = [&] <class T> (T value) { };
    auto longTemplate = [&] <class FirstParameter, class SecondParameter, class ThirdParameter> (
        FirstParameter first,
        SecondParameter second,
        ThirdParameter third) { };
    auto captured = [firstLongCaptureName, secondLongCaptureName, thirdLongCaptureName] <class T> (T value) { };
}
