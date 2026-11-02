TEST_F(Fixture, Scenario){
    Run(config, components);
}
static DEFINE_CASE( Suite , Nested(1+2, (3+4)))   {
    Run(config, components);
}
struct Holder {
    DEFINE_CASE(Suite, Member)
    {
        Run(config, components);
    }
};
void Ordinary(){
    Run(config, components);
}
UNLISTED_CASE(Suite, Opaque)   {Run(config, components);}
TEST_F(Fixture, DeclarationOnly);
// hit-fmt off
TEST_F(Fixture, Protected){Run(config, components);}
// hit-fmt on
