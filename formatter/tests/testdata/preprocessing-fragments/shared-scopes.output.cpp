namespace detail {
class Guard
{
public:
    ~Guard()
    {
#if FEATURE
        if (ready) {
#elif ALTERNATIVE
        if (alternate) {
#else
        if (fallback) {
#endif
            Release();
        }
    }
    void Run()
    {
        Work();
    }
    void Stop()
    {
        Done();
    }
};
struct Worker
{
    ~Worker()
    {
#if FEATURE
        Release();
#else
        Reset();
#endif
    }
    void Run()
    {
#ifndef FEATURE
        while (ready) {
#else
        while (fallback) {
#endif
            Poll();
        }
    }
    void Stop()
    {
        Done();
    }
};
}
