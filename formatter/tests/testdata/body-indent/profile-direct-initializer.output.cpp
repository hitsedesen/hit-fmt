void foo()
{
    static Foo::Bar::Baz variable({"foo", "bar", "baz"});
}

int gnu_statement_expression()
{
    return ({
        int value = 1;
        value + 1;
    });
}
