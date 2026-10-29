namespace n_core::n_detail {
class CWidget {};
struct SNode {};
struct ExternalNode;
union UData { int m_value; };
enum class EState { Ready };
template <typename T> concept CValid = true;
void BadExternal(int badParam);
int s_count, m_total;
void fwork(int s_arg) { int m_local = s_arg; BadExternal(m_local); }

class Widget {};
struct Snode {};
struct DRecord {};
union uData {};
enum class State {};
template <typename T> concept valid = true;
void Process(int arg) {}
int count, m_Bad;
namespace wrong { }
int (*bad_pointer)(int);

// Ambiguous expressions, calls, aliases and constructors are not reported.
class CFactory { CFactory(); ~CFactory(); operator int(); };
void fskip() { unknown * uncertain; int (*handler)(int); }
using Alias = int;
typedef int Legacy;
#define MAKE(x) struct x
namespace alias = n_core;
} // namespace n_core

// External names are checked only when defined outside exempt namespaces.
class ExternalClass;
union ExternalUnion;
enum class ExternalEnum;
template <typename T> struct ExternalTemplate;
void ExternalFunction(int externalParameter);
template <typename T> void BadTemplate(T m_value) {}
template void BadTemplate<int>(int);
extern template void BadTemplate<long>(long);
template <typename T> T BadVariable;
template int BadVariable<int>;
namespace std {
struct ExternalDefinition { int badMember; };
void ExternalDefinitionFunction(int badParameter) {}
namespace chrono { enum class ExternalEnum { value }; }
}
namespace std::ranges { struct BadRange {}; }
namespace n_core::third_party {
class ExternalClass {};
void ExternalFunction(int badParameter) {}
}
namespace standard { class ExternalClass {}; }

class CReader {
public:
    explicit CReader(int s_limit = 0) {}
    constexpr CReader(bool badParameter) {}
    virtual ~CReader() {}
    explicit operator bool() const { return true; }
    int BadMethod() { return 0; }
    static CReader BadFactory() { return CReader(); }
};
