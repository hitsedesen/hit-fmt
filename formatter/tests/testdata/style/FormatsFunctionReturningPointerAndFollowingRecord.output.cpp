template <class Value>
Value (*Select(Operation operation))(Value, Value)
{
    return nullptr;
}

class Fixture
    : public Base
    , public Contract<std::tuple<Operation, Value, Value>>
{
protected:
    void Reset() override
    { }
};
