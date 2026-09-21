void function()
{
    output(build()
        .beginMap()
            .item("first").value(firstValue)
            // Keep this compatibility field for older readers.
            .item("second").value(secondValue)
            .item("third").value(thirdValue)
        .endMap());
}
