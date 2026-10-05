struct Host {
    struct Value;
    struct Pointer;
    struct Lvalue;
    struct Rvalue;
    struct Callback;
    struct Aligned;
    struct Extended;
};
struct [[nodiscard]] Host::Value {
    int stored;
    Value(int v): stored(v) {
    }
};
template<class T> class Deferred;
template<> class [[nodiscard]] Deferred<void> {
};
struct [[maybe_unused]] [[deprecated("legacy")]] Decorated {
};
struct Host::Pointer {
    int* stored;
    Pointer(int* v): stored(v) {
    }
};
struct Host::Lvalue {
    const int& stored;
    Lvalue(const int& v): stored(v) {
    }
};
struct Host::Rvalue {
    int stored;
    Rvalue(int&& v): stored(v) {
    }
};
struct Host::Callback {
    Callback(void (*cb)(int&)) {
        cb(value);
    }
    int value;
    void (*callback)(int&);
};
struct Plain {
    Plain(int* p, int& l, int&& r, int v) {
    }
};
namespace Scope {
struct Box {
    struct Item;
};
}
struct Scope::Box::Item {
    Item(int& v) {
    }
};
template<class T> struct Family {
    struct Child;
};
template<class T> struct Family<T>::Child {
    Child(T* v) {
    }
    Child* next;
    void use() {
        Child* copy = next;
    }
};
template<class T> struct Spec;
template<> struct Spec<int> {
    Spec(int& v) {
    }
};
template<class T> struct Container {
    template<class U> struct Entry;
};
template<class T> template<class U> struct Container<T>::Entry {
    Entry(U&& v) {
    }
};
void accept(void (*)(int&), void (&)(int*));
void nested(int (*cb)(int&)) {
    int (*local)(int&) = cb;
}
void ordinary(int* cb, int& value);
void invoke(int& value) {
    ordinary(nullptr, value);
}
constexpr int Alignment = 16;
struct alignas(Alignment) Host::Aligned {
    Aligned(int& v) {
    }
};
struct alignas(16) NoGap {
};
class alignas(alignof(NoGap)) Nested {
};
union alignas(16) alignas(32) Repeated {
    int value;
};
template<class T> struct alignas(T) Typed {
};
struct alignas(16) Forward;
struct alignas(16) [[nodiscard]] Attributed {
};
struct alignas(16) /* alignment */ Commented {
};
struct alignas(1<< 4) Shifted {
};
class alignas((1< 2) ?16 :32) Selected {
};
union alignas(alignof(int)+ sizeof(long)) alignas(combine(8, 16)) RepeatedExpressions {
    int value;
};
struct alignas(factory<int, long>(1+ 2)) Templated;
struct alignas /* operand */ (1+ 15) CommentedExpression {
};
template<class... Types> struct alignas(Types...) TypePack {
};
template<int... Values> struct alignas((Values+ 16)...) ValuePack {
};
struct alignas(int*) PointerType {
};
struct __attribute__((aligned(16)))Host::Extended {
    Extended(int* v) {
    }
};
class Vessel {
    public:
    class Leaf;
};
class Vessel::Leaf: public Plain {
    public:
    explicit Leaf(int& v): Plain(nullptr, v, 0, 0) {
    }
    ~Leaf() {
    }
};
template<> struct Family<Spec<int>>::Child {
    Child(int* v) {
    }
};
int Tally = 0;
struct Ledger {
    struct Tally;
};
struct Ledger::Tally {
};
void update_tally() {
    Tally += 1;
}
int EntryCount = 0;
template<class T> struct Shelf {
    struct EntryCount;
};
template<class T> struct Shelf<T>::EntryCount {
    EntryCount(T* v) {
    }
    EntryCount* next;
};
void update_entries() {
    EntryCount += 1;
}
namespace ForwardCollision {
int Pending = 0;
struct Owner {
    struct Pending;
};
struct Owner::Pending;
void update() {
    Pending += 1;
}
}
namespace EmptyTemplateCollision {
int Entry = 0;
template<class T> struct Owner {
    struct Entry;
};
template<class T> struct Owner<T>::Entry {
};
void update() {
    Entry += 1;
}
}
namespace ForwardTemplateCollision {
int Pending = 0;
template<class T> struct Owner {
    struct Pending;
};
template<> struct Owner<int>::Pending;
void update() {
    Pending += 1;
}
}
struct ExportedEmpty {
};
struct ExportedForward;
template<class T> struct ExportedTemplateEmpty {
};
template<class T> struct ExportedTemplateForward;
void keep_exports() {
    Plain* nonempty;
    ExportedEmpty* empty;
    ExportedForward* forward;
    ExportedTemplateEmpty<int>* template_empty;
    ExportedTemplateForward<int>* template_forward;
    ::ExportedEmpty * global_empty;
    ::ExportedForward ** global_forward;
    ::ExportedEmpty & global_reference;
    ::ExportedEmpty && global_rvalue;
    ::ExportedTemplateEmpty<int> * global_template;
    ::Scope::Box::Item* qualified_unknown;
    ::External* unknown_global;
    int ExportedEmpty;
    ::ExportedEmpty * shadowed_global_type;
}
int factor;
void global_values(int operand, int mask, bool condition) {
    using factor = int;
    ::factor*operand;
    ::factor&mask;
    ::factor&&condition;
    ::factor* 2;
    ::factor& 7;
    ::factor&& true;
    ::invoke(operand);
}
