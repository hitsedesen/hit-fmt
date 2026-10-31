namespace NChecks {
int GlobalCount;
int badGlobal;
static int StaticGlobalCount;
static int badStaticGlobal;
void External(int BadDeclarationParameter);
void Process(int value, int BadParameter)
{
    int localCount, BadLocal;
    const int localConstant = 0;
    constexpr int BadConstant = 0;
    static int StaticCount;
    static int badStatic;
    auto Logger = value;
    auto OtherLogger = value;
    int (*callback)(int);
    int (*BadCallback)(int);
    auto [first, BadBinding] = Pair();
    for (auto [key, BadKey] : Pairs()) {}
    for (int BadIndex : Values()) {}
    auto action = [] (int BadLambdaParameter) { int BadLambdaLocal; };
    struct TLocal { int Count; int badMember; };
}
class TCounter
{
    int Count_;
    int MissingSuffix;
    int lower_;
    static int SharedCount;
    static int badShared;
protected:
    int ProtectedCount_;
    int ProtectedMissing;
public:
    int Count;
    int badPublic;
    static int PublicSharedCount;
    static int badPublicShared;
    TCounter(int BadConstructorParameter) {}
    void Work(int BadMethodParameter) { int localCount; static int badStatic; }
    class TNested { int Count_; int Missing; };
    int CountAgain;
};
struct TRecord
{
    int Count;
    int badDefaultPublic;
private:
    int Count_;
    int Missing;
};
union TUnion
{
    int Count;
    int badUnionField;
};
class TConditional
{
#if ENABLED
public:
    int Count;
    int badBranchPublic;
#else
public:
    int OtherCount;
#endif
    int AfterBranches;
    int badAfterBranches;
#if FEATURE
private:
    int PrivateCount_;
    int MissingBranchSuffix;
#endif
    int AccessUnknown;
private:
#if FEATURE
    int MissingPrivateSuffix;
#endif
    int StillPrivateMissing;
};
enum class EPhase { Ready, bad_phase };
DEFINE_ENUM(EState,
    (Active)
    (bad_state)
);
DEFINE_BIT_ENUM(BadState,
    ((Enabled)  (1))
    ((bad_flag) (2))
);
TEST(Suite, Case) { int BadMacroLocal; }
BEGIN_SETTER(Counter)
public:
    int Count;
    int badGeneratedPublic;
private:
    int Count_;
    int MissingGeneratedSuffix;
END_SETTER()
}
namespace std { struct foreign_type { int foreign_field; }; }
