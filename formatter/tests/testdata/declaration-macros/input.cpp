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
class Fourth { int member; } instance;

Declare(First) Declare(Second) union Fifth { int number; float fraction; };

Declare(First)
/* between invocations */
Declare(Second) // before declaration
struct Forward;

Declare(First)
Declare(Second)
enum class Choice { One, Two };

Declare(Only)
struct Single { int value; };

Declare(First);
Declare(Second);
struct Terminated { int value; };

Declare(First)
Declare(Second)
int variable;

Declare(First)
Declare(Second)
Result annotated(int value) { return value; }

Declare(First)
Declare(Second) { opaque(); }
}
