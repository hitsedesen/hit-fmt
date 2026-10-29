void Process(
    const Request& request,
    const Settings& settings
) {
    Prepare();
    Execute();
}
void Reset() {
    Prepare();
    Execute();
}
Worker::Worker() :
    Cache_(CreateCache()),
    Index_(CreateIndex())
{
    Prepare();
    Execute();
}
Worker::Worker(Request request) {
    Prepare();
    Execute();
}
Worker::~Worker() {
    Prepare();
    Execute();
}
Worker::Worker(
    Request request
) try :
    Cache_(CreateCache()),
    Index_(CreateIndex())
{
    Prepare();
    Execute();
} catch (...) {
    Recover();
    Report();
}
struct Derived :
    FirstBase,
    SecondBase
{
    void Run(
        Request request,
        Settings settings
    ) {
        Prepare();
        Execute();
    }
    void Nested() {
        if (Ready()) {
            Prepare();
            Execute();
        } else if (Enabled()) {
            Prepare();
            Execute();
        }
        auto callback = [](
            Request request,
            Settings settings
        ) {
            Prepare();
            Execute();
        };
    }
};
void WithComment(
    Request request
) // keep this comment
{
    Prepare();
    Execute();
}
