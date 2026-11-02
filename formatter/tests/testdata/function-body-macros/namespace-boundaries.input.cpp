EXPORT TEST_F() {}

Result
OrdinaryFunction() {return {};}

MACRO_NAMESPACE_BEGIN

TEST_F() {}

MACRO_NAMESPACE_END

OpenScope
// independent test
DEFINE_CASE(Suite, First) {auto count=left+right; Use(count);}
CloseScope

OpenScope
DEFINE_CASE(Suite, Second) {}
CloseScope
