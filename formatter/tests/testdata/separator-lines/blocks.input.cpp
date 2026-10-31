namespace NExample {
/////
using namespace NCore;
using namespace NTools; // imported tools

// Envelope documentation.
template <class T>
struct TEnvelope {
    ///////////////
    struct TNested { int Value; };
    void Reset() {
        ///////////////
        Value = {};
    }
    T Value;
};
TEnvelope<int> CreateEnvelope();
void ConsumeEnvelope(const TEnvelope<int>& envelope);

// The next independent type owns this comment.
enum class EState { Idle, Ready };
/////////
DEFINE_ENUM(EFlag,
    (First)
    ///////////////
    (Second));
class TWorker {};
using TSize = int;
using TIndex = int;
int Count;
int Capacity;
int (*Callback)(int value);
static int Generation;
void Start();
void Stop();
////////
void Restart();
namespace NDetail {
using namespace NCore;
struct TDetail {};
}
inline namespace NVersion {
namespace {
void Work() { Start(); }
}
}
namespace NEmpty {}
namespace NEmptyWithNull {;}
namespace NAlias = NCore;
}
