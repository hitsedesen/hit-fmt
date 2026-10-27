struct TMockDispatcher
{
    MOCK_METHOD(
        int,
        Dispatch,
        (int request, int limit),
        (const, override));
};

TEST(TDispatcherTest, CompletesRequests)
{
    auto result = ReadRequest();
    if (result) {
        CompleteRequest(result);
    }
}

TYPED_TEST(TTypedDispatcherTest, KeepsTestHeader)
{
    auto result = ReadTypedRequest<TypeParam>();
    if (result) {
        CompleteRequest(result);
    }
}
