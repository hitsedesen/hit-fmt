void Run()
{
    Source()
        .Begin()
        .End();
    auto result = Source()
        .Begin()
        .End();
    Source()
        ->Open()
        .Close();
    Source()
        (1)
        (2);
    Consume(Source());
    receiver.Value_ = nullptr;
    receiver->Value_ = nullptr;
    Consume(receiver.Child.Value_);
    receiver.Open();
    receiver.Open(Compute());
    receiver.Child.Open();
    Factory().Open();
    Factory()->Open();
    scope::Factory().Open();
    scope::Factory()->Open();
    scope::Factory<Item>().Open();
    Factory().Child.Open();
    Factory().Open().Value = nullptr;
    Factory().Open(Compute().Read());
    Factory()(argument);
    Inspect(
        receiver.primarySectionWithLongDescriptiveName.secondarySectionWithLongDescriptiveName
            .finalPropertyWithLongDescriptiveName);
    auto nested = Wrap( // Outer argument.
        Wrap( // Inner argument.
            Source()
                .Begin()
                .End()));
}
auto Peek()
{
    return CurrentSlot()->Get();
}
class AnnotatedDispatchAdapter
{
public:
    void Invoke(Closure callback) override
    {
        OutboundInvoker_->Invoke(
            BIND_WRAPPED(&AnnotatedDispatchAdapter::RunCallback, MakeStrong(this), Passed(std::move(callback))));
    }
};
struct Slot
{
    mutable void* Value_;
    Slot(const Slot& source)
        : Value_(source.Value_)
    {
        source.Value_ = nullptr;
    }
};
