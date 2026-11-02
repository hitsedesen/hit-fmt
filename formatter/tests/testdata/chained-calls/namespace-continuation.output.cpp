namespace A {
    namespace B {
        void f() {
            auto x = client->Map(Plan()
                .Input<Row>(input)
                .Output<Row>(output), Mapper());
            if (ready) {
                auto y = Plan()
                    .Input<Row>(input)
                        .Output<Row>(output);
            }
        }
    }
}

void outside() {
    auto z = client->Map(Plan()
        .Input<Row>(input)
        .Output<Row>(output), Mapper());
}
