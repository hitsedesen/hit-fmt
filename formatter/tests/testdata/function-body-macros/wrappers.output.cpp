SUITE(Example) {
    TEST(Case) {
        Run(first_argument,
            second_argument,
            third_argument,
            fourth_argument);
    }
    TEST(Second) {
        Second();
    }
    TEST(Third) {
        Third();
    }
}
GROUP /* opaque header */ ( Nested(1+2,(3+4)) ) {
    WRAPPER(Inner) {
        static DEFINE_CASE(
            Another,
            Nested(1+2, (3+4)))
        {
            Run(first_argument,
                second_argument,
                third_argument,
                fourth_argument);
        }
        DEFINE_CASE(Second) {
            Second();
        }
        // A comment between sibling tests stays at the wrapper's level.
        TEST(Third) {
            Third();
        }
    }
    TEST(Following) {
        Run(first_argument,
            second_argument,
            third_argument,
            fourth_argument);
    }
}
SUITE(Payloads) {
    PAYLOAD(TEST(Argument) {Run(first_argument, second_argument, third_argument, fourth_argument);});
    auto value = Holder{TEST(Initializer) {Run(first_argument, second_argument, third_argument, fourth_argument);}};
    auto expression = TEST(value);
    TEST(DeclarationOnly);
    TEST(AfterPayloads) {
        Run(first_argument,
            second_argument,
            third_argument,
            fourth_argument);
    }
    UNLISTED(Case) {Run(first_argument, second_argument, third_argument, fourth_argument);}
}
namespace Comparison {
    TEST(Case) {
        Run(first_argument,
            second_argument,
            third_argument,
            fourth_argument);
    }
    TEST(Second) {
        Second();
    }
}
