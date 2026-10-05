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
}
