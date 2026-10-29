int Read() {
    return 1;
}
void Reset() {
    Clear();
    Notify();
}
void Empty() {}
void Commented() { /* reason */ Clear();
}
void Long() {
    ReportAnUnusuallyLongMessage(
        "this statement does not fit on one line");
}
struct Worker {
    Worker() {
        Start();
    }
    ~Worker() {
        Stop();
    }
    void Run() {
        Tick();
    }
};
int Recover() try {
    return Read();
} catch (...) { return 0; }
void Branch(bool ready) {
    if (ready) { Clear(); } else { Notify(); }
    if (ready)Clear();
    else Notify();
    if (ready) {
        Clear();
        Notify();
    }
    if (ready) {}
    if (ready) { /* reason */ Clear();
    }
    if (ready) [[likely]] { Clear(); }
    for (int i = 0; i < 2; ++i) { Tick(); }
    for (auto item : items)Use(item);
    while (ready) { Tick(); }
    do { Tick(); } while (ready);
    do Tick();
    while (ready);
    if (ready)
        while (ready)Tick();
    switch (value) {
        case 1: { Tick(); }
        default:
            break;
    }
    try { Tick(); } catch (...) { Recover(); }
    {
        Tick();
    }
    auto callback = [] {
        Tick();
        Notify();
    };
}
auto callback = [] {
    Tick();
    Notify();
};
auto empty = [] {};
DEFINE_TASK(Run) {
    Tick();
}
struct Value {
    int value;
};
namespace Scope {
int value;
}
