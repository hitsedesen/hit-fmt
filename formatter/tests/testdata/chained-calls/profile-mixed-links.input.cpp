void Run()
{
    state.DeferredCallbacks->front()();
    state.DeferredCallbacks->front()(argument);
    Receiver().Open()->Close();
    Receiver()->Open().Close();
    Receiver().Open()();
    Receiver()->Open()();
    receiver.Open().Close();
    receiver->Open()->Close();
    Factory()(1)(2);
    receiver.Field.Child.Open();
    receiver->Field->Child->Open();
    receiver.Field->Child.Value;
    receiver.First().Second()->Third();
    receiver->First()->Second().Third();
    receiver.First()->Second().Third()->Fourth();
    receiver.First().Field.Second();
    receiver.First().Second()(argument);
    Receiver()
        .First()
        // This does not change the link kind.
        .Second();
    namespace_name /* Keep this qualifier comment. */ :: Factory().First().Second();
    state.aVeryLongDescriptiveFieldNameWithCallbacks->front()(aVeryLongDescriptiveArgumentName);
}
