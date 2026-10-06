Value values[] = {
    Value::First,
    Value::Last // last
};

Value plain[] = {
    first,
    last // plain
};

Value members[] = {
    source.first,
    source.last // member
};

Value compound[] = {
    Value::First,
    offset + Value::Last // compound
};

Value blocks[] = {
    Value::First,
    Value::Last /* last */ /* extra */
};

Value mixed[] = {
    Value::First,
    Value::Last /* last */ // extra
};

Value standalone[] = {
    Value::First,
    Value::Last
    // last
};

Value existing[] = {
    Value::First,
    Value::Last, // last
};

Value separated[] = {
    Value::First,
    Value::Last /* last */, // extra
};

Value internal[] = {
    Value::First,
    Value:: /* internal */ Last // last
};

Value nested[][1] = {
    {Value::First},
    {Value::Last} // last
};
