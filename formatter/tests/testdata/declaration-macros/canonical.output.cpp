namespace Example {
DECLARE(First)
Declare(Second)
struct Third
{
    int value;
};

announce(One)
REGISTER(Two)
Describe(Three)
class Fourth
{
    int member;
} instance;

Declare(First) Declare(Second) union Fifth
{
    int number;
    float fraction;
};

Declare(First)
/* between invocations */
Declare(Second) // before declaration
struct Forward;

Declare(First)
Declare(Second)
enum class Choice
{ One, Two };

Declare(Only)
struct Single
{
    int value;
};

Declare(First);
Declare(Second);
struct Terminated
{
    int value;
};

Declare(First)
Declare(Second)
int variable;

Declare(First)
Declare(Second)
Result annotated(int value) { return value; }

Declare(First)
Declare(Second) { opaque(); }
}

MACRO_NAMESPACE_BEGIN
namespace {
int value = 1;
} // namespace
MACRO_NAMESPACE_END

OpenScope
/* namespace boundary */
namespace Detail {
void Run( ) {
    Call(1, 2);
}
}
CloseScope

OpenScope
inline namespace Version {
using namespace Detail;
}
CloseScope

OpenScope
namespace Alias = Detail;
CloseScope

OpenScope
namespace {
}
CloseScope

ScopeBegin
class Metadata
{
    public:
    int Get(int value) {
        return value;
    }
    private:
    int stored = 2;
};
ScopeEnd

OpenScope
// independent record
struct Data
{
    int first = 1;
    int second = 2;
};
CloseScope

OpenScope
union Payload
{
    int number;
    float fraction;
};
CloseScope

OpenScope
template<class T>
class Wrapper
{
    T value;
};
CloseScope

OpenScope
enum class State
{ First, Second };
CloseScope

OpenScope
class Forward;
CloseScope

EXPORT class SameLine { int member=3; };
typedef
struct Alias { int member; } Alias;
const
struct Data* pointer;
Data make(class Forward* value);
