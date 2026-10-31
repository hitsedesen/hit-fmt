void declared(/*parameter=*/ int value);

void demo() {
    consume(/* note */value, /*count=*/ size, /* name = */  next);
    consume(/*_item2=*/ value, /*имя=*/ value, /*=*/ value, /*2name=*/ value);
    consume(/*count*/size, /* name */  next, /*_item2*/ value, /*имя*/value);
    consume(/* */ value, /*2name*/ value, /*two words*/ value, /*x+y*/ value);
    consume(/* explanation = */value, /*two words=*/ value, /*x+y=*/ value, /*name==*/ value);
    consume(/* note */ /*count=*/ size, /*count=*/ /* note */value);
    consume(value /*count=*/, value /* first */ /*last=*/);
    consume(/*count=*/{first, last}, /*empty=*/ {}, /*value=*/Type{first});
    auto values = {/* note */value, /*count=*/ size, /*range=*/{first, last}};
    auto bareLabels = {/*count*/size, /*range*/{first, last}, value /*count*/};
    consume(/*count=*/
        value, /* ordinary */
        next);
    consume(// line comment
        /*count=*/ value);
    consume(/*count=*/);
    auto empty = {/*count=*/};
    // clang-format off
    consume(/*count=*/  value, /* note */value);
    consume(/*count*/  value);
    // clang-format on
}
