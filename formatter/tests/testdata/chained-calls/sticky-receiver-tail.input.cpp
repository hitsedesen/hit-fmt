void F()
{
    registrar.Parameter("x").Default(1).GreaterThan(0);
    registrar->Parameter("x")->Default(1)->GreaterThan(0);
    registrar.Parameter("x").Default(1);
    registrar.Parameter("x");
    registrar.Other("x").Default(1).GreaterThan(0);
    other.Parameter("x").Default(1).GreaterThan(0);
}
