struct Registry
{
public:
    DECLARE_TYPED_ACTION(
        FirstConfigurationType,
        SecondConfigurationType,
        (OptionalVeryLongSerializerType));
    DECLARE_TYPED_ACTION_EXTRA(FirstConfigurationType, SecondConfigurationType, (OptionalVeryLongSerializerType));
};
