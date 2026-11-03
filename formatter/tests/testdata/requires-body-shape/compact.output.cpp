void Example() {
    if constexpr (requires { request.body.state; }) {
        Consume(request.body.state);
    }
}
struct ExampleRecord {
    int value;
};
