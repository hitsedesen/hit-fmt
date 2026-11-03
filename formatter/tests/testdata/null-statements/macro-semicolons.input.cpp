namespace Nested {
TEST(Suite, Nested) { ; Work(); };
int next;
}

extern "C" {
TEST(Suite, Linked) { Work(); };
int linked;
}

TEST(Suite, Comment) { Work(); }; // keep comment
int afterComment;

TEST(Suite, BeforeUnknown) { Work(); };
UNKNOWN(Record) {};
;
int after;

TEST(Suite, Recovery) { Invoke(left,,right); };

struct Record {};
enum class Choice { First };
void ordinary() { do { Work(); } while (again); }

TEST(Suite, Multiple) { Work(); };;
