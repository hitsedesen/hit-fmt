struct Host
{
    struct Value;
    struct Pointer;
    struct Lvalue;
    struct Rvalue;
    struct Callback;
    struct Aligned;
    struct Extended;
};
struct[[nodiscard]]Host::Value
{
    int stored;
    Value(int v): stored(v)
    { }
};
struct Host::Pointer
{
    int* stored;
    Pointer(int* v): stored(v)
    { }
};
struct Host::Lvalue
{
    const int& stored;
    Lvalue(const int& v): stored(v)
    { }
};
struct Host::Rvalue
{
    int stored;
    Rvalue(int&& v): stored(v)
    { }
};
struct Host::Callback
{
    Callback(void (*cb)(int&))
    {
        cb(value);
    }
    int value;
    void (*callback)(int&);
};
struct Plain
{
    Plain(int* p, int& l, int&& r, int v)
    { }
};
namespace Scope {
struct Box
{
    struct Item;
};
} // namespace Scope
struct Scope::Box::Item
{
    Item(int& v)
    { }
};
template <class T> struct Family
{
    struct Child;
};
template <class T> struct Family<T>::Child
{
    Child(T* v)
    { }
    Child* next;
    void use()
    {
        Child* copy = next;
    }
};
template <class T> struct Spec;
template <> struct Spec<int>
{
    Spec(int& v)
    { }
};
template <class T> struct Container
{
    template <class U> struct Entry;
};
template <class T> template <class U> struct Container<T>::Entry
{
    Entry(U&& v)
    { }
};
void accept(void (*)(int&), void (&)(int*));
void nested(int (*cb)(int&))
{
    int (*local)(int&) = cb;
}
void ordinary(int* cb, int& value);
void invoke(int& value)
{
    ordinary(nullptr, value);
}
constexpr int Alignment = 16;
struct alignas (Alignment)Host::Aligned
{
    Aligned(int& v)
    { }
};
struct __attribute__((aligned(16)))Host::Extended
{
    Extended(int* v)
    { }
};
class Vessel
{
public:
    class Leaf;
};
class Vessel::Leaf
    : public Plain
{
public:
    explicit Leaf(int& v): Plain(nullptr, v, 0, 0)
    { }
    ~Leaf()
    { }
};
template <> struct Family<Spec<int>>::Child
{
    Child(int* v)
    { }
};
int Tally = 0;
struct Ledger
{
    struct Tally;
};
struct Ledger::Tally
{ };
void update_tally()
{
    Tally += 1;
}
int EntryCount = 0;
template <class T> struct Shelf
{
    struct EntryCount;
};
template <class T> struct Shelf<T>::EntryCount
{
    EntryCount(T* v)
    { }
    EntryCount* next;
};
void update_entries()
{
    EntryCount += 1;
}
namespace ForwardCollision {
int Pending = 0;
struct Owner
{
    struct Pending;
};
struct Owner::Pending;
void update()
{
    Pending += 1;
}
} // namespace ForwardCollision
namespace EmptyTemplateCollision {
int Entry = 0;
template <class T> struct Owner
{
    struct Entry;
};
template <class T> struct Owner<T>::Entry
{ };
void update()
{
    Entry += 1;
}
} // namespace EmptyTemplateCollision
namespace ForwardTemplateCollision {
int Pending = 0;
template <class T> struct Owner
{
    struct Pending;
};
template <> struct Owner<int>::Pending;
void update()
{
    Pending += 1;
}
} // namespace ForwardTemplateCollision
struct ExportedEmpty
{ };
struct ExportedForward;
template <class T> struct ExportedTemplateEmpty
{ };
template <class T> struct ExportedTemplateForward;
void keep_exports()
{
    Plain* nonempty;
    ExportedEmpty* empty;
    ExportedForward* forward;
    ExportedTemplateEmpty<int>* template_empty;
    ExportedTemplateForward<int>* template_forward;
}
