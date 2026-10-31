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

namespace NRegressions {
class TLauncher {};
void Exercise(const int& Logger, int this_, int _, int BadParameter)
{
    TLauncher().Launch();
    TLauncher{}.Launch();
    { auto this_ = GetOwner(); }
    for (auto [key, _] : Pairs()) {}
    unsigned char unsignedCh = 0;
    unsigned char BadCharacter = 0;
    union { int localCount; int BadLocalUnion; };
}
class TStorage
{
    union { int Count_; int Missing; };
    union [[maybe_unused]] { int TaggedCount_; int TaggedMissing; };
    union alignas(16) { int AlignedCount_; int AlignedMissing; };
    union { int Visible; int badVisible; } Container_;
    union TNamed { int Count; int badNamed; };
    NLogging::TLogger Logger;
protected:
    union { int ProtectedCount_; int ProtectedMissing; };
public:
    union { int Count; int badPublicUnion; };
};
class TDerivedStorage : public TStorage
{
protected:
    NLogging::TLogger Logger;
};
DECLARE_CHANNEL(TChannel, Channel, .Configure(1).Enable());
REGISTER_OBJECT(TObject, Object, MakeRegistry().WithScope("scope"));
DECLARE_KIND(TNode)
DECLARE_HANDLE(THandle)
DEFINE_ENUM(InvalidStage,
    (Ready)
    (bad_stage)
);
}
