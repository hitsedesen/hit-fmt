void Convert()
{
    messages.push_back(
        static_cast<Foo::Bar::Message>(
        Foo::Bar::GetValue<i64>(message.Message, "message_type")));
    consume(
        dynamic_cast<Outer::Inner*>(
        lookup(value)));
    consume(
        reinterpret_cast<Outer::Inner*>(
        lookup(value)));
    consume(
        const_cast<Outer::Inner*>(
        lookup(value)));
    consume(
        static_cast<long>(
        static_cast<int>(
        lookup(value))));
    consume(
        static_cast<long>(
        // operand stays attached
        lookup(value)
        ));
    consume(static_cast<int>(value));
    consume((int)value);
    consume(static_cast<Id<bool, T>>(value));
    consume(first, static_cast<Id<bool, T>>(value), last);
    consume(const_cast<Id<bool, T>*>(pointer));
    consume(reinterpret_cast<Id<bool, T>*>(pointer));
    consume(dynamic_cast<Id<bool, T>*>(pointer));
    consume(static_cast<Outer<Id<bool, T>>>(value));
    consume(static_cast /* type */<Id<bool, T>>(value));
    consume(static_cast<Id<bool, T>>(value) > limit, last);
    consume(left < right, middle >> shift);
    consume(left < middle, right > (value));
}

auto ConvertValue()
{
    return static_cast<Id<bool, T>>(value);
}
