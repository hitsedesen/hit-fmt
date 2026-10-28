struct Request;
struct Response;
void Send();
void Receive();
int sent;
int received;
namespace Api {
struct Record {};
enum Kind { First };
enum Kind kind;
int afterEnum;
enum Kind *kindPointer;
enum Api::Kind qualified;
enum Kind braced{First};
enum Kind copy = First;
enum Kind GetKind();
void ReadKind();
enum class State : unsigned;
enum class State : unsigned { Idle };
enum State state;
int afterState;
typedef int Count;
using Size = unsigned;
template<class T> struct Box;
template<class T> using Handle = T*;
// Factory documentation.
template<class T> void Create();
void Reset() {}
template<class T> int total;
auto callback = [] {};
void (*handler)();
extern void (&reference)();
int initialized(1);
void (*MakeHandler())();
void More();
int mixed(), object;
int afterMixed;
using Api::Reset;
void AfterUsing();
struct Members {
    struct Nested;
    using Value = int;
    void Read();
    void Write();
    int left;
    int right;
    public:
    void Access();
    int exposed;
};
}
export struct Exported;
export template<class T> struct ExportedBox;
export void Publish();
export int version;
export {
struct Visible;
void Expose();
int visible;
}
extern "C" {
struct Context;
void Entry();
int status;
}
void BeforeBarrier();
static_assert(true);
int afterBarrier;
void BeforeDirective();
#if ENABLED
int conditional;
#endif
int afterDirective;
void BeforeComment();
// Separate section.

int afterComment;
// hit-fmt off
struct  Protected;
void  ProtectedFunction();
// hit-fmt on
int afterProtection;
void Local() {
    struct LocalType;
    void LocalFunction();
    int local;
}
