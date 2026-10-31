namespace NMailbox {
namespace {

////////////////////////////////////////////////////////////////////////////////

template <class T> struct TEnvelope
{
    T Value;
};
enum class EDelivery {Waiting, Ready};
class TMailbox
{
public:
    TMailbox()
    { }

    int Size() const
    {
        return Size_;
    }

    template <class T> requires CReady<T>
    T Wait();

private:
    int Size_ = 0;
    int Capacity_ = 8;


    void Reset()
    {
        Size_ = 0;
    }
};
template <class T> requires CAvailable<T>
T Acquire();
template <class T>
// The constraint belongs to the following declaration.
requires CAccepted<T>
struct TAccepted
{
    T Value;
};
void Touch()
{
    WakeUp();
}
void Clear()
{
    ResetQueue();
}
void Schedule();



void Cancel();
void Visit()
{
    Before();

    ///////////////

    After();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NMailbox
