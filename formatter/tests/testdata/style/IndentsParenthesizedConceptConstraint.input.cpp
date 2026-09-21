template <class Value>
concept ScalarComparable =
    std::equality_comparable<Value> && (
        std::same_as<Value, String> ||
        std::same_as<Value, Buffer> ||
        std::integral<Value> ||
        std::is_enum_v<Value>);
