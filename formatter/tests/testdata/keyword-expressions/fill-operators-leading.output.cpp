bool Test()
{
    return F("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")
        || F("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")
        || F([] (int x) {
            return x;
        });
}

bool NestedCall()
{
    return F("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")
        || F("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")
        || Check(first_argument_with_an_extended_name, second_argument_with_an_extended_name);
}

bool Conditions()
{
    return first_condition && second_condition && third_condition && fourth_condition && fifth_condition
        && sixth_condition && seventh_condition;
}

bool NestedInitializer()
{
    return F("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")
        || F("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")
        || Check({
            foo, // Keep the initializer multiline.
            bar,
            baz,
        });
}

bool Compare()
{
    return std::max({
        first_value_with_an_extended_name,
        second_value_with_an_extended_name,
        third_value_with_an_extended_name,
    }) > 1;
}
