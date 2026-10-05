template <class Element> struct Bucket : Parent<Element>, Other {
public:
    unsigned flags : 3;
    int field;
};
class Shell {};
union Cell {};
enum class Shade : unsigned { Light, Dark };
namespace scope { int item; int next; }
struct Engine : BaseEngine {
    [[nodiscard]] int compute(int value, int *pointer) const & noexcept(noexcept(value)) override;
};
[[]] int annotated;
int (*callback)(int);
int Holder::*member;
using Deduced = decltype(1);
alignas(16) int aligned;
static_assert(sizeof(int) > 0);
template<class... Pack> void examine(Pack... arguments) {
    Bucket<int> object{};
    int values[2];
    extern int incomplete[];
    auto [first, second] = object;
    auto capture = [first, item = second](int arg) mutable {
        first();
        second();
        return arg;
    };
    auto empty = [](){};
    []{}();
    compute(first, second);
    compute();
    /* attached */ [[maybe_unused]] int attached;
    (opaque)(first, second);
    Cell aggregate{1, 2};
    auto allocation = new (arena) Cell(first);
    auto braceAllocation = new Cell{first};
    auto arrayAllocation = new int[2];
    auto emptyArrayAllocation = new int[]{};
    auto unconfirmedAllocation = new decltype(values[0])[2];
    delete [] arrayAllocation;
    auto converted = static_cast<int>(first);
    auto cStyle = (int) first;
    auto grouped = (first + second);
    auto unary = not first;
    first++;
    first = first and second ? first : second;
    auto folded = (arguments + ... + 0);
    auto size = sizeof(int) + alignof(int) + sizeof...(Pack);
    auto identity = typeid(int);
    auto ready = co_await (first);
    return (first);
    co_return (second);
    throw (first);
    if (int condition = first; condition) { first++; }
    for (int index = 0; index < 2; index++) {}
    for (auto entry : values) {}
    while (first) { break; }
label:
    object.field = values[0];
    scope::item = first; // trailing
}
int operator+(int value, Cell other);
struct Callable { int operator()(int value); };
int emptyParameters();
auto trailing(int value) -> int;
auto requirements = requires(int value) { value + value; };
auto allocateEmpty = new Cell();
struct Constructed : Parent<int> {
    Constructed() : first(1), second(2) {}
};
import <package/sub.h>;
#define FEATURE_LEVEL 1
export /* scope */ {
    int exposed;
    /* member */ [[maybe_unused]] int another;
}
template<class... Values> void captureAssign(Values... a) {
    auto nested = [&...x=(a=1)]{};
    auto chain = [...x=a=1]{};
    auto defaults = [=]{};
    do {} while (ready)  ;
}
void guarded()try {} catch(...) {}
struct Guard { Guard() try : value(1){} catch(...){} int value; };
struct RefCallable { void run() &; };
