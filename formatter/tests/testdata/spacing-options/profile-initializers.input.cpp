void Example()
{
    writer.Field = {55, 34,};
    record.Names = { "alpha", "beta" };
    record.Codes = {10, 20};
    record.Groups = {{ "red", "blue" }, {"green", "yellow"}};
    int frames[][4] = {
        {
            WrapCode(11), WrapCode(12), WrapCode(13), WrapCode(14)
        }, {
            WrapCode(21), WrapCode(22), WrapCode(23), WrapCode(24)
        }
    };
}

template <typename T, int Steps>
    requires std::is_arithmetic_v<T>
TRollingGauge<T, Steps> operator+(const TRollingGauge<T, Steps>& left, const TRollingGauge<T, Steps>& right)
{
    return left;
}

template <typename T, int Steps>
    requires std::is_arithmetic_v<T>
TRollingGauge<T, Steps>& operator+=(TRollingGauge<T, Steps>& left, const TRollingGauge<T, Steps>& right)
{
    return left;
}
