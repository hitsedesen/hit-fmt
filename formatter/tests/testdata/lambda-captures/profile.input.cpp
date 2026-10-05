void Make()
{
    auto values = MakeGroup(
        {
            MakeCallback([&] {
                return 42;
            })
        },
        2);
    GetDispatcher()
        .Subscribe(MakeCallback([this, observer = MakeObserver(this)] (const VeryLongResponseType<SharedPointer<Message>>& response) {
            Observe(response);
        }));
    auto cancel = RegisterHandler(BIND_NO_PROPAGATE([weakRequest = MakeWeak(request), ticket = MakeTicketForRequest(request), this, weakHandler = MakeWeak(handler)] (const TError& error) {
        Notify(error);
    }));
}

void BuildSelector()
{
    auto selector = BindSelector(
        [
        firstCount, secondCount, thirdCount
        ] {
            return firstCount + secondCount + thirdCount;
        })
    .WithShelf()
        .WithPrice();
}

void Schedule()
{
    auto handler = MakeHandler([
            state,
            connection = std::move(connection)
        ] () mutable {
            state->Prepare();
            connection->Subscribe(MakeCallback([
                state,
                guard = std::move(guard)
            ] (const TResult&) mutable {
                state->Finish();
                guard->Notify();
            }));
        });
}

auto AdjustLater()
{
    return WhenAllComplete(workItems).Apply(BIND([this, weakHandle = MakeWeak(this), count] (const TError& /*ignored*/ ) {
        if (auto current = weakHandle.Lock()) {
            current->Adjust(count);
        }
    }));
}

class Adapter : public Wrapper<false> {
public:
    void Invoke(Closure callback) override {
        if (Closed_) {
            callback.Reset();
            return;
        }
        return Target_->Invoke(BIND_WRAPPED(
            [
                this,
                keep = Hold(this),
                callback = std::move(callback)
            ] () mutable {
                auto guard = Detail::MakeGuard(Context_);

                if (Context_->Closed_) {
                    callback.Reset();
                    return;
                }

                {
                    InvokerGuard invokerGuard(this);
                    ContextGuard contextGuard(Context_);
                    callback();
                }
            }));
    }
};
