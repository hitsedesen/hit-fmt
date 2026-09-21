typedef bool (*ValidationHook)(const Record* record, size_t passCount);

class Accumulator
{
public:
    Accumulator& operator+=(const Accumulator& other);
};

template <class T>
void Consume(T* value, const T& other)
{
    const T* first = value, *second = first;
    const T& left = other, &right = left;
    auto callback = [&other] {
        return *value;
    };
    auto generatedValue = (*ValueFactory_)();
    auto shortCallback = [&other] { return *value; };
    RunBatch<containers::RingBuffer<long>>("ring");
    RunBatch<LocalStack<long>>("stack");
    for (const auto& item : values) {
        callback(item);
    }
}
