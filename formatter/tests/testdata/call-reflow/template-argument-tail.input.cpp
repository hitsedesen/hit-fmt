void Register()
{
    auto parameter = Create<TParameter<TValue>>(
        key,
        std::make_unique<TFieldAccessor<TBase, TValue>>(field), fieldIndex, typeid(TRecord));
    auto member = Consume(firstArgument, factory.Make<Left, Right>(value), fieldIndex, lastArgument);
    auto typed = Consume(firstArgument, Make<int>(value, other), fieldIndex, lastArgument);
    auto chained = Consume(firstArgument, helpers::Make<Left, Right>(value).Get(index), fieldIndex, lastArgument);
    auto commented = Consume(firstArgument, helpers::Make<Left, Right> /* keep */ (value), fieldIndex, lastArgument);
    auto tailComment = Consume(firstArgument, helpers::Make<Left, Right>(value), /* tail */ fieldIndex, lastArgument);
    auto fitting = Consume(key, Make<Left, Right>(value), index);
    auto compared = Consume(firstArgument, left < right, limit > (value), fieldIndex, lastArgument);
    auto conditional = Consume(firstArgument, left < right ? value, limit > (other), next : fallback, lastArgument);
    auto templateConditional = Consume(firstArgument, helpers::Make<flag ? left, right : other>(value), fieldIndex, lastArgument);
    auto uncertain = Consume(firstArgument, left < right, limit > value, fieldIndex, lastArgument);
}
