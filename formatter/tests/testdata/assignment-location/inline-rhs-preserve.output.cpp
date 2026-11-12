void F() {
    auto x = Max(
        longVarName,
        longVarName2);
    x = Max(longVarName, longVarName2);
    auto resultWithLongName = foo
        .bar()
        .baz()
        .daz();
    resultWithLongName = foo
        .bar()
        .baz()
        .daz();
    auto small = Max(a, b);
    auto chain = foo.bar().baz();
    auto literal = "unbreakable_literal_that_fits";
    auto variableWithAnExtendedName = Max(
        longVarName,
        longVarName2);
}
