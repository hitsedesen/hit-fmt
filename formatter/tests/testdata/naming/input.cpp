namespace n_core::n_detail {
class CWidget {};
struct SNode {};
struct CNode;
union UData { int m_value; };
enum class EState { Ready };
template <typename T> concept CValid = true;
void fprocess(int m_arg);
int s_count, m_total;
void fwork(int s_arg) { int m_local = s_arg; fprocess(m_local); }

class Widget {};
struct Snode {};
struct DRecord {};
union uData {};
enum class State {};
template <typename T> concept valid = true;
void Process(int arg);
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
