#pragma once

#include <span>
#include <string_view>

namespace hit_fmt {

struct HelpExample
{
    std::string_view Title;
    std::string_view Code;
};

struct HelpSection
{
    std::string_view Title;
    std::string_view Description;
    std::span<const HelpExample> Examples;
};

// hit-fmt off
constexpr HelpExample HelpExamples0[] = {
    {R"HIT_FMT_HELP(IndentWidth (with IndentMembers = Indent):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentWidth = 2
struct S {
  int value;
};
// IndentWidth = 4
struct S {
    int value;
};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ColumnLimit (with CallArgumentSplit = OnePerLine):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ColumnLimit = 80: fits on one line
send(first, second, third);
// ColumnLimit = 16: splits at the limit
send(first,
    second,
    third);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(LineLengthCheck (with ColumnLimit = 10):)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: a single unbreakable identifier wider than 10 columns
// LineLengthCheck = Preserve: no line-length check (default)
// LineLengthCheck = Strict: line-length violation
// LineLengthCheck = Soft: no length violation if no safe fix exists
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentationKind (with IndentMembers = Indent, IndentWidth = 4):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentationKind = Spaces
    int value;
// IndentationKind = Tabs (the leading indentation is one tab)
	int value;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TrailingWhitespace:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: "int x;  " (two trailing spaces)
// TrailingWhitespace = Trim: "int x;"
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples1[] = {
    {R"HIT_FMT_HELP(IncludeOrder:)HIT_FMT_HELP", R"HIT_FMT_HELP(// IncludeOrder = Sort
#include "alpha.h"
#include "zeta.h"
#include <vector>
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(PreprocessorIndentation (with IndentWidth = 4):)HIT_FMT_HELP", R"HIT_FMT_HELP(// PreprocessorIndentation = LineStart
#if X
#define Y 1
#endif
// PreprocessorIndentation = PreprocessorIndented
#if X
    #define Y 1
#endif
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TrailingCommentSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// TrailingCommentSpacing = Space
int n; // comment
// TrailingCommentSpacing = NoSpace
int n;// comment
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SeparatorLength:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SeparatorLength = 12 (existing standalone ruler)
////////////
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples2[] = {
    {R"HIT_FMT_HELP(TemplateKeywordSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// TemplateKeywordSpacing = Space
template <class T> struct S {};
// TemplateKeywordSpacing = NoSpace
template<class T> struct S {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TemplateTypeArgumentInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// TemplateTypeArgumentInteriorSpacing = Space
vector< int > values;
// TemplateTypeArgumentInteriorSpacing = NoSpace
vector<int> values;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TemplateTypeArgumentSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// TemplateTypeArgumentSpacing = Space
vector <int> values;
// TemplateTypeArgumentSpacing = NoSpace
vector<int> values;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AmbiguousTemplateDeclarationSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AmbiguousTemplateDeclarationSpacing = Space
Type<int> name = value;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(RequirementBodyInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// RequirementBodyInteriorSpacing = Space
concept C = requires(T x) { x; };
// RequirementBodyInteriorSpacing = NoSpace
concept C = requires(T x) {x;};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AttributeSpaceAfterSpecifier:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AttributeSpaceAfterSpecifier = Space
[[nodiscard]] int f();
// AttributeSpaceAfterSpecifier = NoSpace
[[nodiscard]]int f();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AttributeSpaceBeforeSpecifier:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AttributeSpaceBeforeSpecifier = Space
int [[maybe_unused]] x;
// AttributeSpaceBeforeSpecifier = NoSpace
int[[maybe_unused]] x;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyAttributeInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyAttributeInteriorSpacing = Space
[[ ]]
// EmptyAttributeInteriorSpacing = NoSpace
[[]]
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AttributeInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AttributeInteriorSpacing = Space
[[ nodiscard ]] int f();
// AttributeInteriorSpacing = NoSpace
[[nodiscard]] int f();
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples3[] = {
    {R"HIT_FMT_HELP(RecordBodyInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// RecordBodyInteriorSpacing = Space
struct S { int n; };
// RecordBodyInteriorSpacing = NoSpace
struct S {int n;};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NamespaceBodyInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NamespaceBodyInteriorSpacing = Space
namespace N { int n; }
// NamespaceBodyInteriorSpacing = NoSpace
namespace N {int n;}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EnumBodyInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EnumBodyInteriorSpacing = Space
enum E { A };
// EnumBodyInteriorSpacing = NoSpace
enum E {A};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AccessModifierColonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AccessModifierColonSpacing = Space
struct S { public : int n; };
// AccessModifierColonSpacing = NoSpace
struct S { public:int n; };
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(RangeBasedForColonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// RangeBasedForColonSpacing = Space
for (auto x : items) {}
// RangeBasedForColonSpacing = NoSpace
for (auto x:items) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(BitFieldColonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// BitFieldColonSpacing = NoSpace | Left | Right | LeftAndRight
int bits:1;  int bits :1;  int bits: 1;  int bits : 1;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EnumUnderlyingTypeColonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EnumUnderlyingTypeColonSpacing = NoSpace | LeftAndRight
enum E:int {};  enum E : int {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(LabelColonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// LabelColonSpacing = NoSpace | LeftAndRight
case 1:break;  case 1 : break;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(PointerReferenceDeclaratorLocation:)HIT_FMT_HELP", R"HIT_FMT_HELP(// PointerReferenceDeclaratorLocation = Left | Middle | Right
int* p;  int * p;  int *p;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NestedDeclaratorInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NestedDeclaratorInteriorSpacing = Space | NoSpace
int ( *callback )();  int (*callback)();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NestedDeclaratorPointerSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NestedDeclaratorPointerSpacing = Space | NoSpace
int (* callback)();  int (*callback)();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(MemberPointerDeclaratorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// MemberPointerDeclaratorSpacing = Space | NoSpace
int S::* member;  int S::*member;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ClassBracePlacement, StructBracePlacement, UnionBracePlacement, FunctionBracePlacement, LambdaBracePlacement, and NamespaceBracePlacement (set each independently):)HIT_FMT_HELP", R"HIT_FMT_HELP(// StructBracePlacement = Attach
struct S {};
// StructBracePlacement = NewLine
struct S
{};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ClassBraceSpacing, StructBraceSpacing, UnionBraceSpacing, FunctionBraceSpacing, LambdaBraceSpacing, and NamespaceBraceSpacing (when attaching a brace):)HIT_FMT_HELP", R"HIT_FMT_HELP(// StructBraceSpacing = SingleSpace; StructBracePlacement = Attach
struct S {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EnforceClassBraceSpacing, EnforceStructBraceSpacing, EnforceUnionBraceSpacing, EnforceFunctionBraceSpacing, EnforceLambdaBraceSpacing, and EnforceNamespaceBraceSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EnforceStructBraceSpacing = true; StructBraceSpacing = SingleSpace
struct S {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EnumBraceSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EnumBraceSpacing = SingleSpace
enum E {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NamespaceComments:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NamespaceComments = AddMissing
namespace N {
int x;
} // namespace N
// NamespaceComments = Remove
namespace N {
int x;
}
// NamespaceComments = Fix (corrects an existing wrong comment)
namespace N {
int x;
} // namespace N
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NamespaceCommentSpaces (with NamespaceComments = AddMissing):)HIT_FMT_HELP", R"HIT_FMT_HELP(// NamespaceCommentSpaces = 2
namespace N {
int x;
}  // namespace N
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ShortNamespaceLines (with NamespaceComments = AddMissing):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ShortNamespaceLines = 1: no comment added to this short namespace
namespace N {
int x;
}
// ShortNamespaceLines = 0: comment added
namespace N {
int x;
} // namespace N
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentAccessModifiers (with AccessModifierIndentWidth = 2):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentAccessModifiers = Indent
struct S {
  public:
};
// IndentAccessModifiers = NoIndent
struct S {
public:
};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AccessModifierIndentWidth (with IndentAccessModifiers = Indent):)HIT_FMT_HELP", R"HIT_FMT_HELP(// AccessModifierIndentWidth = 4
struct S {
    public:
};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentMembers (with MemberIndentWidth = 2):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentMembers = Indent
struct S {
  int n;
};
// IndentMembers = NoIndent
struct S {
int n;
};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(MemberIndentWidth (with IndentMembers = Indent):)HIT_FMT_HELP", R"HIT_FMT_HELP(// MemberIndentWidth = 4
struct S {
    int n;
};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceList:)HIT_FMT_HELP", R"HIT_FMT_HELP(// InheritanceList = Format
struct S
    : public Base,
    public Other
{};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListColonPlacement (with InheritanceList = Format):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InheritanceListColonPlacement = AttachToBase
struct S
    : public Base {};
// InheritanceListColonPlacement = AttachToClass
struct S :
    public Base {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListColonSpaceBefore / InheritanceListColonSpaceAfter (with InheritanceList = Format, InheritanceListStart = ClassLine):)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceBefore = Space; SpaceAfter = Space
struct S : public Base {};
// SpaceBefore = NoSpace; SpaceAfter = NoSpace
struct S:public Base {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListCommaPlacement (with InheritanceList = Format):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InheritanceListCommaPlacement = AttachToNext
struct S : public Base
    , public Other {};
// InheritanceListCommaPlacement = AttachToPrevious
struct S : public Base,
    public Other {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListCommaSpaceBefore / InheritanceListCommaSpaceAfter (with InheritanceList = Format, InheritanceListStart = ClassLine, InheritanceListSplit = Pack):)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceBefore = NoSpace; SpaceAfter = Space
struct S : public A, public B {};
// SpaceBefore = Space; SpaceAfter = NoSpace
struct S : public A ,public B {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListStart (with InheritanceList = Format):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InheritanceListStart = ClassLine
struct S : public Base {};
// InheritanceListStart = NewLine
struct S
    : public Base {};
// NewLineOnOverflow uses ClassLine until the full list exceeds ColumnLimit.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListSplit (with InheritanceList = Format):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InheritanceListSplit = OnePerLine
struct S : public A,
    public B {};
// InheritanceListSplit = Pack (when both fit)
struct S : public A, public B {};
// OnePerLineOnOverflow splits only when the full list exceeds ColumnLimit.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InheritanceListIndent (with InheritanceList = Format):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InheritanceListIndent = 2
struct S
  : public A
  , public B {};
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples4[] = {
    {R"HIT_FMT_HELP(FunctionSuffixSpecifierSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionSuffixSpecifierSpacing = Space | NoSpace
void f() override;  void f()override;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NoexceptSpaceBeforeSpecifier:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NoexceptSpaceBeforeSpecifier = Space | NoSpace
void f() noexcept;  void f()noexcept;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionQualifierSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionQualifierSpacing = Space | NoSpace
void f() const;  void f()const;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionRefQualifierSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionRefQualifierSpacing = Space | NoSpace
void f() const &;  void f() const&;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ReturnArrowSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ReturnArrowSpacing = NoSpace | Left | Right | LeftAndRight
auto f()->T;  auto f() ->T;  auto f()-> T;  auto f() -> T;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionParameterClosingParenthesis and EnforceFunctionParameterClosingParenthesis:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionParameterClosingParenthesis = SameLine
void f(int first,
    int second);
// FunctionParameterClosingParenthesis = NewLine
void f(int first,
    int second
);
// EnforceFunctionParameterClosingParenthesis = true also moves an
// existing safe closing parenthesis without reflowing the list.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionCallArgumentClosingParenthesis and EnforceFunctionCallArgumentClosingParenthesis:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionCallArgumentClosingParenthesis = SameLine
f(first,
    second);
// FunctionCallArgumentClosingParenthesis = NewLine
f(first,
    second
);
// EnforceFunctionCallArgumentClosingParenthesis = true also moves an
// existing safe closing parenthesis without reflowing the list.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionParameterSplit (with a sufficiently small ColumnLimit):)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionParameterSplit = Pack
void send(int first, int second,
    int third);
// FunctionParameterSplit = OnePerLine
void send(int first,
    int second,
    int third);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FirstFunctionParameterPosition (when splitting):)HIT_FMT_HELP", R"HIT_FMT_HELP(// FirstFunctionParameterPosition = NameLine
void send(int first,
    int second);
// FirstFunctionParameterPosition = NewLine
void send(
    int first,
    int second);
// NameLineIfFits keeps the first beside '(' only if it fits.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EnforceFirstFunctionParameterPosition:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EnforceFirstFunctionParameterPosition = true
// FirstFunctionParameterPosition = NewLine (already split input)
void send(
    int first,
    int second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionParameterIndent:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionParameterIndent = IndentWidth (4)
void send(int first,
    int second);
// FunctionParameterIndent = Continuation
void send(int first,
          int second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionParameterNewLineIndent:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionParameterNewLineIndent = 2; first parameter on NewLine
void send(
  int first,
  int second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(MergeFunctionParameters:)HIT_FMT_HELP", R"HIT_FMT_HELP(// MergeFunctionParameters = false (already split input)
void f(int a,
    int b);
// MergeFunctionParameters = true (when the flat list fits)
void f(int a, int b);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionSpaceBeforeParameters:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionSpaceBeforeParameters = Space | NoSpace
void f (int n);  void f(int n);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SpaceInsideFunctionParameters and EnforceSpaceInsideFunctionParameters:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceInsideFunctionParameters = Space
// EnforceSpaceInsideFunctionParameters = true
void f( int n );
// SpaceInsideFunctionParameters = NoSpace
// EnforceSpaceInsideFunctionParameters = true
void f(int n);
// Without enforcement, only reflowed lists change.
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples5[] = {
    {R"HIT_FMT_HELP(EmptyLambdaBodySpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyLambdaBodySpacing = Space | NoSpace
auto f = [] { };  auto f = [] {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SingleStatementLambdaBodyLayout:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SingleStatementLambdaBodyLayout = OneLineIfFits
auto f = [] { return 1; };
// An existing safe multiline body is joined if it fits.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(LambdaSpaceBeforeParameters:)HIT_FMT_HELP", R"HIT_FMT_HELP(// LambdaSpaceBeforeParameters = Space | NoSpace
auto f = [] (int x) {};  auto f = [](int x) {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SpaceInsideLambdaParameters:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceInsideLambdaParameters = Space | NoSpace
[]( int x ) {};  [](int x) {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SpaceAroundCaptureList:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceAroundCaptureList = Space | NoSpace
[ value ] {};  [value] {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SpaceAroundCaptureAssignment:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceAroundCaptureAssignment = Space | NoSpace
[x = 1] {};  [x=1] {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(LambdaCaptureSplit (when the capture list exceeds ColumnLimit):)HIT_FMT_HELP", R"HIT_FMT_HELP(// LambdaCaptureSplit = OnePerLineOnOverflow
[shortName,
    longerName
]() {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(LambdaCallSpaceBeforeArguments:)HIT_FMT_HELP", R"HIT_FMT_HELP(// LambdaCallSpaceBeforeArguments = Space | NoSpace
[] {} (1);  [] {}(1);
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples6[] = {
    {R"HIT_FMT_HELP(EmptyFunctionCallArgumentsSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyFunctionCallArgumentsSpacing = Space | NoSpace
f( );  f();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyFunctionParametersSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyFunctionParametersSpacing = Space | NoSpace
void f( );  void f();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyLambdaParametersSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyLambdaParametersSpacing = Space | NoSpace
[]( ) {};  []() {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyLambdaCaptureSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyLambdaCaptureSpacing = Space | NoSpace
[ ]() {};  []() {};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CallArgumentSplit (when the call exceeds ColumnLimit):)HIT_FMT_HELP", R"HIT_FMT_HELP(// CallArgumentSplit = Pack
send(first, second,
    third);
// CallArgumentSplit = OnePerLine
send(first,
    second,
    third);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CallArgumentSplitColumnLimit and CallArgumentSplitColumnLimitNames:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ColumnLimit = 80; CallArgumentSplitColumnLimit = 20
// CallArgumentSplitColumnLimitNames = ["send"]
// CallArgumentSplit = OnePerLine
send(firstArgument,
    secondArgument);
other(firstArgument, secondArgument); // keeps the global limit
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FirstCallArgumentPosition (when splitting):)HIT_FMT_HELP", R"HIT_FMT_HELP(// FirstCallArgumentPosition = CallLine
send(first,
    second);
// FirstCallArgumentPosition = NewLine
send(
    first,
    second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(MultilineSingleCallArgumentPosition and EnforceMultilineSingleCallArgumentPosition:)HIT_FMT_HELP", R"HIT_FMT_HELP(// MultilineSingleCallArgumentPosition = NewLine
send(
    first +
        second);
// Newly generated breaks move the sole argument even without enforcement.
// EnforceMultilineSingleCallArgumentPosition = true also moves
// an existing safe multiline sole argument.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ChainedCallRootNames:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ChainedCallRootNames = ["MakeAction"]
MakeAction()
    .First()
    .Second();
// With [], that chain stays as written.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ChainedCallSplitCondition (with the root configured):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ChainedCallSplitCondition = Always
MakeAction()
    .First()
    .Second();
// Overflow only splits when a chain line exceeds ColumnLimit.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ChainedCallLayout (when the chain must split):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ChainedCallLayout = OnePerLine
MakeAction()
    .First()
    .Second();
// ChainedCallLayout = Pack (if both calls fit)
MakeAction()
    .First().Second();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ChainedCallIndent (with IndentWidth = 4):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ChainedCallIndent = IndentWidth
MakeAction()
    .First();
// ChainedCallIndent = Continuation
MakeAction()
            .First();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CallLikeMacroNames:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CallLikeMacroNames = ["DECLARE_CALLBACK"]
// FunctionParameterSplit = OnePerLine; ColumnLimit small
DECLARE_CALLBACK(int first,
    int second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(WhitespaceSensitiveMacros:)HIT_FMT_HELP", R"HIT_FMT_HELP(// WhitespaceSensitiveMacros = ["STRINGIFY"]
STRINGIFY( x , y ); // invocation kept byte-for-byte
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CallLineFirstStringNames:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CallLineFirstStringNames = ["LOG"]
// CallArgumentSplit = OnePerLine; ColumnLimit small
LOG("message",
    first,
    second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EnforceFirstCallArgumentPosition:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EnforceFirstCallArgumentPosition = true
// FirstCallArgumentPosition = NewLine (already split call)
send(
    first,
    second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CallArgumentIndent (with IndentWidth = 4):)HIT_FMT_HELP", R"HIT_FMT_HELP(// CallArgumentIndent = IndentWidth
send(first,
    second);
// CallArgumentIndent = Continuation
send(first,
     second);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionCallSpaceBeforeParameters:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionCallSpaceBeforeParameters = Space | NoSpace
send (x);  send(x);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SpaceInsideCallArguments and EnforceSpaceInsideCallArguments:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceInsideCallArguments = Space; EnforceSpaceInsideCallArguments = true
send( first, second );
// SpaceInsideCallArguments = NoSpace; EnforceSpaceInsideCallArguments = true
send(first, second);
// Without enforcement, only reflowed lists change.
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples7[] = {
    {R"HIT_FMT_HELP(ControlBlockConditionSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ControlBlockConditionSpacing = Space | NoSpace
if ( x ) {}  if (x) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ControlBlockSpaceBeforeBrace and EnforceControlBlockSpaceBeforeBrace:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ControlBlockSpaceBeforeBrace = Space
// EnforceControlBlockSpaceBeforeBrace = true
if (x) {}
// ControlBlockSpaceBeforeBrace = NoSpace
// EnforceControlBlockSpaceBeforeBrace = true
if (x){}
// Without enforcement, only newly generated braces change.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(LoopSemicolonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// LoopSemicolonSpacing = Space | NoSpace
for (int i=0; i<n; ++i) {}  for (int i=0;i<n;++i) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConditionInitSemicolonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ConditionInitSemicolonSpacing = Space | NoSpace
if (auto x=f(); x) {}  if (auto x=f();x) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AddMissingControlBraces:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: if (ready) run();
// AddMissingControlBraces = false
if (ready) run();
// AddMissingControlBraces = true
if (ready) { run(); }
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyControlBody:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: while (busy());
// EmptyControlBody = EmptyBlock
while (busy()) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(RemoveNullStatements:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: void f() { work(); ; }
// RemoveNullStatements = true
void f() { work(); }
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(RemoveNullDeclarations:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: namespace N {}; void f() {};
// RemoveNullDeclarations = true
namespace N {} void f() {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentCaseLabels (with CaseLabelIndentWidth = 2):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentCaseLabels = NoIndent
switch (x) {
case 1: break;
}
// IndentCaseLabels = Indent
switch (x) {
  case 1: break;
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CaseLabelIndentWidth (with IndentCaseLabels = Indent):)HIT_FMT_HELP", R"HIT_FMT_HELP(// CaseLabelIndentWidth = 4
switch (x) {
    case 1: break;
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ControlHeaderClauseSplit:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ControlHeaderClauseSplit = WhenNeeded; ColumnLimit small
for (int index = 0;
    index < count;
    ++index) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ForHeaderClauseIndent (with ControlHeaderClauseSplit = WhenNeeded):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ForHeaderClauseIndent = IndentWidth (4)
for (int i = 0;
    i < n;
    ++i) {}
// ForHeaderClauseIndent = Continuation
for (int i = 0;
     i < n;
     ++i) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ControlConditionIndent (with ExpressionLayoutCondition = Overflow):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ControlConditionIndent = IndentWidth (2)
if (first &&
  second) {}
// ControlConditionIndent = Continuation
if (first &&
    second) {} // aligns after '('
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ControlBlockKeywordSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ControlBlockKeywordSpacing = Space | NoSpace
if (ready) {}  if(ready) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ControlBlockBracePlacement and EnforceControlBlockBracePlacement:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ControlBlockBracePlacement = SameLine
// EnforceControlBlockBracePlacement = true
if (ready) {}
// ControlBlockBracePlacement = NewLine
// EnforceControlBlockBracePlacement = true
if (ready)
{}
// AttachUnlessMultiline keeps a multiline header's brace on a new line.
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples8[] = {
    {R"HIT_FMT_HELP(BlockInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// BlockInteriorSpacing = Space | NoSpace
void f() { work(); }  void f() {work();}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyBlockInteriorSpacing and EnforceEmptyBlockInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyBlockInteriorSpacing = Space
// EnforceEmptyBlockInteriorSpacing = true
void f() { }
// EmptyBlockInteriorSpacing = NoSpace
// EnforceEmptyBlockInteriorSpacing = true
void f() {}
// Without enforcement, only newly generated empty blocks change.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentBlockBodies:)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentBlockBodies = false (input indentation retained)
void f() {
work();
}
// IndentBlockBodies = true; IndentWidth = 4
void f() {
    work();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(BlockBodyIndentWidth (with IndentBlockBodies = true):)HIT_FMT_HELP", R"HIT_FMT_HELP(// BlockBodyIndentWidth = 2
void f() {
  work();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentLeadingStatementComments (with IndentBlockBodies = true):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentLeadingStatementComments = true
void f() {
    // work follows
    work();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(IndentedCallScopePairs (with IndentBlockBodies = true):)HIT_FMT_HELP", R"HIT_FMT_HELP(// IndentedCallScopePairs = ["Begin:End"]
void f() {
    obj.Begin();
        obj.Item(); // existing extra level is retained
    obj.End();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(StandaloneBlockBraceIndentation (with IndentBlockBodies = true):)HIT_FMT_HELP", R"HIT_FMT_HELP(// StandaloneBlockBraceIndentation = Indent
void f() {
    {
        work();
    }
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NonEmptyBlockBodyLayout:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NonEmptyBlockBodyLayout = Multiline
void f() {
work();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CollapseEmptyFunctionBodies:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Input: a function with an empty multiline body
// CollapseEmptyFunctionBodies = true
void f() { }
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(BlockClosingBrace and EnforceBlockClosingBrace:)HIT_FMT_HELP", R"HIT_FMT_HELP(// BlockClosingBrace = SameLine; EnforceBlockClosingBrace = true
void f() {
    work(); }
// BlockClosingBrace = NewLine; EnforceBlockClosingBrace = true
void f() {
    work();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(StatementAttributeIndent and StatementAttributeIndentWidth:)HIT_FMT_HELP", R"HIT_FMT_HELP(// StatementAttributeIndent = Block
void f() {
    [[likely]] if (ready) run();
}
// StatementAttributeIndent = Indent; StatementAttributeIndentWidth = 2
void f() {
      [[likely]] if (ready) run();
}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(StatementSemicolonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// StatementSemicolonSpacing = NoSpace
return x;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(DeclarationSemicolonSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// DeclarationSemicolonSpacing = Space | NoSpace
int n ;  int n;
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples9[] = {
    {R"HIT_FMT_HELP(BraceInitializerSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// BraceInitializerSpacing = Space | NoSpace
T x {1};  T x{1};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InitializerBracesSpacing and EnforceInitializerBracesSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// InitializerBracesSpacing = Space
// EnforceInitializerBracesSpacing = true
T x{ 1, 2 };
// InitializerBracesSpacing = NoSpace
// EnforceInitializerBracesSpacing = true
T x{1, 2};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyBraceInitializerInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyBraceInitializerInteriorSpacing = Space | NoSpace
T x{ };  T x{};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InitializerListSplit (with a small ColumnLimit):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InitializerListSplit = Pack (default): break an overflowing list
T x{first, second,
    third};
// InitializerListSplit = OnePerLine: break an overflowing list
T x{first,
    second,
    third};
// EnforceInitializerListSplit = true also normalizes fitting split lists
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FirstInitializerElementPosition (when splitting):)HIT_FMT_HELP", R"HIT_FMT_HELP(// FirstInitializerElementPosition = BraceLine
T x{first,
    second};
// FirstInitializerElementPosition = NewLine
T x{
    first,
    second};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InitializerListIndent (with IndentWidth = 4):)HIT_FMT_HELP", R"HIT_FMT_HELP(// InitializerListIndent = IndentWidth
T x{first,
    second};
// InitializerListIndent = Continuation
T x{first,
    second}; // aligns beneath the first element
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InitializerListClosingBrace and EnforceInitializerListClosingBrace:)HIT_FMT_HELP", R"HIT_FMT_HELP(// InitializerListClosingBrace = SameLine
// EnforceInitializerListClosingBrace = true
T x{first, second,};
// InitializerListClosingBrace = NewLine
T x{first, second
};
// InitializerListClosingBrace = SameLineUnlessTrailingComma (default)
T x{first, second};
T y{
    first,
    second,
};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(InitializerListTrailingComma and EnforceInitializerListTrailingComma:)HIT_FMT_HELP", R"HIT_FMT_HELP(// InitializerListTrailingComma = Keep (default): preserve presence
T x{first, second,};
// InitializerListTrailingComma = Add
// EnforceInitializerListTrailingComma = true
T x{first, second,};
// InitializerListTrailingComma = Remove
// EnforceInitializerListTrailingComma = true
T x{first, second};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(MergeInitializerList:)HIT_FMT_HELP", R"HIT_FMT_HELP(// MergeInitializerList = false
T x{first,
    second};
// MergeInitializerList = true with EnforceInitializerListSplit = true
// (if the result fits)
T x{first, second};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(PlacementNewSpaceBeforeArguments:)HIT_FMT_HELP", R"HIT_FMT_HELP(// PlacementNewSpaceBeforeArguments = Space | NoSpace
new (buffer) T;  new(buffer) T;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(PlacementNewArgumentInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// PlacementNewArgumentInteriorSpacing = Space | NoSpace
new ( buffer ) T;  new (buffer) T;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(PlacementNewSpaceAfterArguments:)HIT_FMT_HELP", R"HIT_FMT_HELP(// PlacementNewSpaceAfterArguments = Space | NoSpace
new(buffer) T;  new(buffer)T;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NewArraySpaceBeforeBrackets:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NewArraySpaceBeforeBrackets = Space | NoSpace
new T [4];  new T[4];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NewArrayInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NewArrayInteriorSpacing = Space | NoSpace
new T[ 4 ];  new T[4];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyArrayDeclarationInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyArrayDeclarationInteriorSpacing = Space | NoSpace
int a[ ];  int a[];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(EmptyNewArrayInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// EmptyNewArrayInteriorSpacing = Space | NoSpace
new T[ ]{1};  new T[]{1};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NewInitializerSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NewInitializerSpaceBeforeParentheses = Space | NoSpace
new T (1);  new T(1);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NewInitializerParenthesisInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NewInitializerParenthesisInteriorSpacing = Space | NoSpace
new T( 1 );  new T(1);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NewInitializerSpaceBeforeBraces:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NewInitializerSpaceBeforeBraces = Space | NoSpace
new T {1};  new T{1};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NewInitializerBraceInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NewInitializerBraceInteriorSpacing = Space | NoSpace
new T{ 1 };  new T{1};
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(DeleteArraySpaceBeforeBrackets:)HIT_FMT_HELP", R"HIT_FMT_HELP(// DeleteArraySpaceBeforeBrackets = Space | NoSpace
delete [] p;  delete[] p;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(DeleteArraySpaceAfterBrackets:)HIT_FMT_HELP", R"HIT_FMT_HELP(// DeleteArraySpaceAfterBrackets = Space | NoSpace
delete[] p;  delete[]p;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerList and ConstructorInitializerListStart:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ConstructorInitializerList = Format; Start = NewLine
S::S()
    : a(1)
    , b(2)
{}
// Start = ConstructorLine
S::S() : a(1), b(2) {}
// NewLineOnOverflow starts on the constructor line if all entries fit.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerListColonPlacement:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AttachToInitializer
S::S()
    : a(1) {}
// AttachToConstructor
S::S() :
    a(1) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerListColonSpaceBefore and ConstructorInitializerListColonSpaceAfter:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceBefore = Space; SpaceAfter = Space
S::S() : a(1) {}
// SpaceBefore = NoSpace; SpaceAfter = NoSpace
S::S():a(1) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerListCommaPlacement:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AttachToNext
S::S() : a(1)
    , b(2) {}
// AttachToPrevious
S::S() : a(1),
    b(2) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerListCommaSpaceBefore and ConstructorInitializerListCommaSpaceAfter:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SpaceBefore = NoSpace; SpaceAfter = Space
S::S() : a(1), b(2) {}
// SpaceBefore = Space; SpaceAfter = NoSpace
S::S() : a(1) ,b(2) {}
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerListSplit:)HIT_FMT_HELP", R"HIT_FMT_HELP(// OnePerLine
S::S() : a(1),
    b(2) {}
// Pack (if both entries fit)
S::S() : a(1), b(2) {}
// OnePerLineOnOverflow splits only when the full list exceeds the limit.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ConstructorInitializerListIndent:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ConstructorInitializerListIndent = 2
S::S()
  : a(1)
  , b(2) {}
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples10[] = {
    {R"HIT_FMT_HELP(NamedCastSpaceBeforeTypeArguments:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NamedCastSpaceBeforeTypeArguments = Space | NoSpace
static_cast <int>(x);  static_cast<int>(x);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NamedCastSpaceBeforeOperandParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NamedCastSpaceBeforeOperandParentheses = Space | NoSpace
static_cast<int> (x);  static_cast<int>(x);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ReturnSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ReturnSpaceBeforeParentheses = Space | NoSpace
return (x);  return(x);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ThrowSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ThrowSpaceBeforeParentheses = Space | NoSpace
throw (x);  throw(x);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CoReturnSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CoReturnSpaceBeforeParentheses = Space | NoSpace
co_return (x);  co_return(x);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(MemberAccessOperatorsSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// MemberAccessOperatorsSpacing = NoSpace
obj.field;  ptr->field;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ScopeResolutionSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ScopeResolutionSpacing = NoSpace
std::vector<int> values;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NoexceptSpaceBeforeParentheses, AlignasSpaceBeforeParentheses, AlignofSpaceBeforeParentheses, SizeofSpaceBeforeParentheses, DecltypeSpaceBeforeParentheses, TypeidSpaceBeforeParentheses, RequiresSpaceBeforeParentheses, and StaticAssertSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// For each keyword's ...SpaceBeforeParentheses = Space
noexcept (x); alignas (8) int n; alignof (T);
sizeof (x); decltype (x); typeid (x);
requires (T x) { x; }; static_assert (true);
// ...SpaceBeforeParentheses = NoSpace
noexcept(x); alignas(8) int n; alignof(T);
sizeof(x); decltype(x); typeid(x);
requires(T x) { x; }; static_assert(true);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SizeofPackSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SizeofPackSpaceBeforeParentheses = Space | NoSpace
sizeof... (Ts);  sizeof...(Ts);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CoAwaitSpaceBeforeParentheses:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CoAwaitSpaceBeforeParentheses = Space | NoSpace
co_await (expr);  co_await(expr);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(OperatorOverloadOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NoSpace | Left | Right | LeftAndRight
operator+();  operator +();  operator+ ();  operator + ();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CastArgumentSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CastArgumentSpacing = Space | NoSpace
( int )x;  (int)x;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(GroupingInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// GroupingInteriorSpacing = Space | NoSpace
( a + b );  (a + b);
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(UnaryOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// UnaryOperatorSpacing = NoSpace
!ready;  *ptr;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(PostfixOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// PostfixOperatorSpacing = NoSpace
index++;  index--;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TernaryOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// TernaryOperatorSpacing = Space
result = ready ? first : second;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(BinaryOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// BinaryOperatorSpacing = Space | NoSpace
a + b;  a+b;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AssignmentOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// AssignmentOperatorSpacing = Space | NoSpace
x = y;  x=y;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CastSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CastSpacing = Space | NoSpace
(int) x;  (int)x;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(CommaSpacing and EnforceCommaSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// CommaSpacing = Space; EnforceCommaSpacing = true
f(a, b);
// CommaSpacing = NoSpace; EnforceCommaSpacing = true
f(a,b);
// Without enforcement, only reflowed lists change.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SubscriptOperatorInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SubscriptOperatorInteriorSpacing = Space | NoSpace
a[ i ];  a[i];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ArrayDeclarationInteriorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ArrayDeclarationInteriorSpacing = Space | NoSpace
int a[ 4 ];  int a[4];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(StructuredBindingBracketSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// StructuredBindingBracketSpacing = Space | NoSpace
auto [ x, y ] = pair;  auto [x, y] = pair;
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(SubscriptOperatorSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// SubscriptOperatorSpacing = Space | NoSpace
a [i];  a[i];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ArrayDeclarationSpacing:)HIT_FMT_HELP", R"HIT_FMT_HELP(// ArrayDeclarationSpacing = Space | NoSpace
int a [4];  int a[4];
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(ExpressionLayoutCondition (with ColumnLimit = 20):)HIT_FMT_HELP", R"HIT_FMT_HELP(// ExpressionLayoutCondition = Overflow (splits an overlong expression)
result = first +
    second + third;
// FitOrOverflow also rejoins safe existing breaks when flat form fits.
// Always starts flat and reflows, even if previously laid out differently.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(DefaultBinaryOperatorPlacement and binary operator lists:)HIT_FMT_HELP", R"HIT_FMT_HELP(// DefaultBinaryOperatorPlacement = Leading
// TrailingBinaryOperatorList = ["+", "<<"]
// LeadingBinaryOperatorList = ["||"]
result = first +
    second;
result = first || second
    || third;
// BinaryOperatorPlacement remains a compatibility alias for the default.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(AssignmentOperatorPlacement (with expression layout enabled):)HIT_FMT_HELP", R"HIT_FMT_HELP(// AssignmentOperatorPlacement = Leading
result
    = calculate();
// AssignmentOperatorPlacement = Trailing
result =
    calculate();
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TernaryOperatorPlacement and EnforceTernaryOperatorPlacement (with expression layout enabled):)HIT_FMT_HELP", R"HIT_FMT_HELP(// TernaryOperatorPlacement = Leading
result = condition
    ? first
    : second;
// TernaryOperatorPlacement = Trailing
result = condition ?
    first :
    second;
// EnforceTernaryOperatorPlacement = true also corrects existing breaks.
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(TernarySplit (with expression layout enabled):)HIT_FMT_HELP", R"HIT_FMT_HELP(// TernarySplit = ThreeLines, when the ternary needs splitting
result = condition
    ? first
    : second;
// AsNeeded may break at either delimiter or both.
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples11[] = {
    {R"HIT_FMT_HELP(<Kind>NameCase (for Class, Enum, Union, Struct, Function, Concept, Variable, or Namespace):)HIT_FMT_HELP", R"HIT_FMT_HELP(// StructNameCase = PascalCase
struct widget {}; // naming violation: expected PascalCase
struct Widget {}; // accepted
// StructNameCase = snake_case
struct widget_name {}; // accepted
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(<Kind>NamePrefixes (independently for each kind):)HIT_FMT_HELP", R"HIT_FMT_HELP(// VariableNamePrefixes = ["m_"]
int value;   // naming violation: missing prefix
int m_value; // accepted
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(NamingNamespaceExemptions:)HIT_FMT_HELP", R"HIT_FMT_HELP(// StructNameCase = PascalCase; NamingNamespaceExemptions = ["vendor"]
namespace vendor { struct widget {}; } // exempt from name checks
namespace app { struct widget {}; }    // naming violation
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionDeclarationParameterNames:)HIT_FMT_HELP", R"HIT_FMT_HELP(// FunctionDeclarationParameterNames = Required
void f(int);       // missing parameter name
void f(int value); // accepted
)HIT_FMT_HELP"},
    {R"HIT_FMT_HELP(FunctionDefinitionParameterNames:)HIT_FMT_HELP", R"HIT_FMT_HELP(// NameRequired: void f(int /*unused*/) {} reports a missing name.
// NameOrCommentedNameRequired: that trailing identifier comment is accepted.
)HIT_FMT_HELP"},
};

constexpr HelpExample HelpExamples12[] = {
    {R"HIT_FMT_HELP(Exclude:)HIT_FMT_HELP", R"HIT_FMT_HELP(// Exclude = ["(^|/)generated(/|$)"]
// src/generated/table.cpp: skipped
// src/handwritten/table.cpp: checked or formatted
)HIT_FMT_HELP"},
};

constexpr HelpSection HelpSections[] = {
    {R"HIT_FMT_HELP(Indentation and limits)HIT_FMT_HELP", R"HIT_FMT_HELP(  • IndentWidth: positive integer up to 32. Used for record bodies and base
  lists, and for parameter or call argument continuation lines when their
  respective indent setting is IndentWidth.
  • ColumnLimit: positive integer up to 10000. Function parameter lists,
  call arguments, expressions, and inheritance lists can be wrapped
  according to their split settings.
  • LineLengthCheck: Preserve (default) disables line-length diagnostics.
  Strict reports every active line exceeding ColumnLimit. Soft reports an
  overlong line only when the formatter can produce a layout at or under
  the limit for that line. If the formatter cannot safely fix it, the
  length alone is not a violation. Line-length checking is independent of
  ExpressionLayoutCondition; operator placement and other diagnostics
  remain governed by expression layout checking.
  • IndentationKind: Preserve (default), Spaces, or Tabs for
  formatter-generated indentation. Preserve and Spaces retain the
  previous space-based output; Tabs emits tabs at IndentWidth-column
  stops, then spaces for any remainder. Existing indentation and comment
  text are not converted. This also affects indentation generated by
  PreprocessorIndentation = PreprocessorIndented.
  • TrailingWhitespace: Preserve (default) leaves line endings intact;
  Trim removes trailing spaces and tabs before LF, CRLF, or end of file.
  Whitespace inside tokens (notably raw strings and preprocessor directives)
  and lines ending in a backslash are preserved to avoid changing a line
  splice.

)HIT_FMT_HELP", HelpExamples0},
    {R"HIT_FMT_HELP(Includes, directives, and comments)HIT_FMT_HELP", R"HIT_FMT_HELP(  • IncludeOrder: Preserve (default) leaves directives untouched. Sort
  stably sorts consecutive, column-one literal #include "..." and
  #include <...> lines by header path, with quoted headers before angled
  headers. It keeps each directive's spelling and the line endings in its
  group. Blank lines, comments, non-include directives (including #if,
  #else, and #endif), macro headers, trailing comments, continued lines,
  and indentation break groups. An include immediately after a comment is
  kept with that comment; later includes may still sort. No includes move
  across these boundaries. This does not infer a main header, deduplicate
  includes, or merge groups.
  • PreprocessorIndentation: Preserve (default), LineStart, or
  PreprocessorIndented. LineStart places single-line directives at
  column zero. PreprocessorIndented does the same for outer directives
  and adds one IndentWidth level after #if, #ifdef, and #ifndef;
  nested directives, including #define, use that level. #elif,
  #else, and #endif align with their opener. This setting changes
  only directive prefixes, not code in branches, directive contents, or
  multiline/continued directives. Unbalanced conditional groups remain
  unchanged in PreprocessorIndented mode.
  • TrailingCommentSpacing: Preserve (default), Space, or NoSpace
  between code and a same-line // comment. Standalone comments, block
  comments, comments within directives, and line breaks stay unchanged;
  this does not align comment columns.
  • SeparatorLength: unset (default) leaves slash rulers unchanged; set to
  an integer from 8 to 10000 to normalize existing, standalone comment
  lines made solely of eight or more / characters. Rulers are also moved
  to column 1, even when their length is already correct. It does not add
  missing separators or alter trailing, documentation, or raw-string
  comments.

)HIT_FMT_HELP", HelpExamples1},
    {R"HIT_FMT_HELP(Templates and attributes)HIT_FMT_HELP", R"HIT_FMT_HELP(  • TemplateKeywordSpacing: Preserve (default), Space, or NoSpace
  between template and < in a complete parsed template declaration,
  including template<>. template disambiguators in expressions are not
  changed.
  • TemplateTypeArgumentInteriorSpacing: Preserve (default), Space, or
  NoSpace just inside a nonempty, complete parsed template parameter or
  argument list in an unambiguous type or template-id context. Comparisons,
  shifts, unknown expressions, >>-terminated lists, and empty <> stay
  unchanged.
  • TemplateTypeArgumentSpacing: Preserve (default), Space, or
  NoSpace before < in a complete, confirmed template argument list
  following an identifier (vector <int> versus vector<int>). This
  also covers empty <>; template parameter lists use
  TemplateKeywordSpacing instead. Multiline and commented gaps are
  preserved.
  • AmbiguousTemplateDeclarationSpacing: Preserve (default) or
  Space. In a conservatively parsed Type<...> name = value; statement,
  Space normalizes the type-to-name gap to one space. It also normalizes
  the name-to-= gap if AssignmentOperatorSpacing = Space. Other
  ambiguous statements remain unchanged.
  • RequirementBodyInteriorSpacing: Preserve (default), Space, or
  NoSpace just inside a parsed requires-expression body, including
  empty bodies: requires(T x){ x; } versus requires(T x){x;}.
  This rule preserves newlines, comments, and recovered syntax.
  • AttributeSpaceAfterSpecifier: Preserve (default), Space, or
  NoSpace between a parsed [[...]] specifier and a following
  declaration word ([[nodiscard]] int f() versus [[nodiscard]]int f()).
  This is independent of AttributeInteriorSpacing.
  • AttributeSpaceBeforeSpecifier: Preserve (default), Space, or
  NoSpace before a parsed [[...]] specifier following a declaration
  token (int [[maybe_unused]] x versus int[[maybe_unused]] x).
  Leading attributes have no preceding declaration gap.
  • EmptyAttributeInteriorSpacing: Preserve (default), Space, or
  NoSpace inside an empty parsed attribute list ([[ ]] versus
  [[]]). AttributeInteriorSpacing controls nonempty lists.
  • AttributeInteriorSpacing: Preserve (default), Space, or
  NoSpace just inside nonempty parsed C++ attributes:
  [[ nodiscard ]] versus [[nodiscard]]. The two bracket pairs are
  kept intact. Multiline and commented gaps are preserved.

)HIT_FMT_HELP", HelpExamples2},
    {R"HIT_FMT_HELP(Records, namespaces, and declarators)HIT_FMT_HELP", R"HIT_FMT_HELP(  • RecordBodyInteriorSpacing, NamespaceBodyInteriorSpacing, and
  EnumBodyInteriorSpacing: Preserve (default), Space, or NoSpace
  just inside their respective parsed braces on one line, including
  empty bodies: struct S{ int x; } versus struct S{int x;} and
  namespace N{ } versus namespace N{}. Class, struct, and union
  bodies share the record option; brace placement is separate.
  • AccessModifierColonSpacing and RangeBasedForColonSpacing:
  Preserve (default), Space, or NoSpace on both same-line sides of
  parsed access-label colons (public : int x; versus public:int x;)
  and range-for separators (for (auto x : items) versus
  for (auto x:items)), respectively. Other colons are unaffected.
  Both colon settings leave multiline, commented, or incomplete gaps
  source-owned.
  • BitFieldColonSpacing, EnumUnderlyingTypeColonSpacing, and
  LabelColonSpacing: Preserve (default), NoSpace (x:1), Left
  (x :1), Right (x: 1), or LeftAndRight (x : 1). The options
  apply only to their parsed bit-field, enum underlying-type, and label
  colons, respectively; LabelColonSpacing covers case and default
  labels as well. Line breaks, comments, and directives are untouched.
  • PointerReferenceDeclaratorLocation: Preserve (default), Left
  (int* p), Middle (int * p), or Right (int *p). Applies only to
  complete parsed pointer and reference declarators, including adjacent
  */& tokens. A later declarator after a comma has no repeated type;
  under Left its */& binds to the name (int* a, *b). Member pointers
  and nested declarators have independent rules. In expression
  contexts, UnaryOperatorSpacing controls prefix dereference/address-of;
  an ambiguous macro argument remains source-owned.
  • NestedDeclaratorInteriorSpacing: Preserve (default), Space, or
  NoSpace just inside complete nested declarator parentheses:
  ( *callback ) versus (*callback).
  • NestedDeclaratorPointerSpacing: Preserve (default), Space, or
  NoSpace between a parsed */& in a nested declarator and its
  immediately following name ((* callback) versus (*callback)).
  • MemberPointerDeclaratorSpacing: Preserve (default), Space, or
  NoSpace between a parsed member-pointer ::* and the following
  declarator name (S::* member versus S::*member). These rules
  preserve multiline and commented gaps and do not alter expressions.
  • *BracePlacement: Attach (default) joins a brace to its header;
  NewLine moves it to its own line. Each matching Enforce*BracePlacement
  option (default false) also changes existing braces. Without enforcement,
  existing braces stay put unless their declaration header is reflowed.
  Existing same-line spacing is left unchanged by Attach. Each of
  class, struct, union, function definition, lambda, and
  namespace is configured independently. Attach places the brace before
  a trailing header comment (// or /* ... */). When NewLine splits an
  inline brace followed by a trailing comment, the comment stays with the
  declaration header and the brace goes below it. Other intervening comments
  and directives block attachment. Other braces are unaffected.
  • *BraceSpacing: SingleSpace (default) selects the spacing when a
  brace is attached to its header. For class, lambda, struct, union,
  function definition, and namespace, the corresponding Enforce*BraceSpacing
  (default false) also normalizes existing same-line gaps. Enum braces
  retain their separate Preserve/SingleSpace setting.
  Line breaks and gaps containing comments or directives remain untouched.
  Placement is applied first: NewLine keeps the brace on its own line;
  Attach joins it to the header, using one space at the join.
  • NamespaceComments: Preserve (default) leaves closing comments alone,
  Remove removes a line comment immediately after a namespace's closing
  brace, AddMissing inserts // namespace name (or // namespace for an
  anonymous namespace) only when no same-line comment is present, and Fix
  also corrects existing trailing line comments. NamespaceCommentSpaces
  sets the spaces before generated or corrected comments (1 by default, positive integer
  up to 32); it has no effect on existing comments with AddMissing, or
  in Preserve and Remove modes. Unmatched braces and closings followed
  by a block comment are left alone.
  • ShortNamespaceLines: nonnegative integer up to 10000. With
  NamespaceComments = AddMissing or Fix, skip adding a closing comment
  when the namespace has at most this many lines between its opening and
  closing braces. 0 (default) disables the exemption. Existing closing
  comments remain untouched with AddMissing and are corrected even on
  short namespaces with Fix; Remove still removes them.
  • IndentAccessModifiers and IndentMembers: Preserve (default)
  leaves indentation unchanged, Indent uses AccessModifierIndentWidth
  or MemberIndentWidth relative to the record declaration, and NoIndent
  aligns with that declaration.
  Access labels and direct members of class, struct, and union bodies use
  independent settings. Enum enumerators use IndentMembers (enums do
  not have access labels). Only existing lines are reindented; nested
  closing braces follow their reindented declarations. Inline members,
  comments, directives, and conditional branches remain source-owned.
  The indent-width overrides for access labels, members, block bodies,
  case labels, and statement attributes accept positive integers up to
  32; 0 (default) uses the global IndentWidth. They affect only
  constructs whose corresponding indentation setting is enabled.
  • InheritanceList: Preserve (default) leaves the entire base list
  unchanged. Format applies the following settings to complete base
  lists in class and struct definitions. Whitespace-only blank lines
  between the header, colon, commas, and bases are normalized. Comments,
  directives, and multiline base specifiers keep a list source-owned.
  Enum underlying-type colons are not inheritance lists.
  • InheritanceListColonPlacement: AttachToBase (default) puts : on
  the first base line; AttachToClass puts it on the class header line.
  InheritanceListColonSpaceBefore uses NoSpace (default) or Space;
  InheritanceListColonSpaceAfter uses Space (default) or NoSpace.
  Spacing applies only to same-line gaps around :.
  • InheritanceListCommaPlacement: AttachToNext (default) puts a comma
  before the next base; AttachToPrevious puts it after the preceding
  base. InheritanceListCommaSpaceBefore uses NoSpace (default) or
  Space; InheritanceListCommaSpaceAfter uses Space (default) or
  NoSpace. Spacing options do not remove line breaks.
  • InheritanceListStart: NewLine (default), ClassLine, or
  NewLineOnOverflow controls the first base's line. The overflow case
  checks whether the entire projected list fits on the class line.
  • InheritanceListSplit: OnePerLine (default) puts each subsequent
  base on its own line; Pack joins bases until the next would exceed
  ColumnLimit; OnePerLineOnOverflow splits only when the entire
  projected list exceeds ColumnLimit. Safe existing line breaks are
  merged whenever the chosen layout permits.
  • InheritanceListIndent: an optional positive integer up to 32 for
  lines created in a base list, relative to the record declaration.
  Defaults to IndentWidth. Format also uses one space after
  public, protected, private, and virtual within each base
  specifier; other internal spacing remains source-owned.

)HIT_FMT_HELP", HelpExamples3},
    {R"HIT_FMT_HELP(Functions and parameters)HIT_FMT_HELP", R"HIT_FMT_HELP(  • FunctionSuffixSpecifierSpacing: Preserve (default), Space, or
  NoSpace before parsed function override/final and lambda
  mutable/constexpr/consteval/static suffixes. For example,
  f() override versus f()override and []() mutable versus
  []()mutable. Gaps between two words cannot be removed when doing
  so would merge their tokens.
  • NoexceptSpaceBeforeSpecifier: Preserve (default), Space, or
  NoSpace before a parsed function or lambda exception specifier:
  void f() noexcept versus void f()noexcept. The gap after
  noexcept before its optional ( uses
  NoexceptSpaceBeforeParentheses. Multiline and commented gaps are
  preserved by these options.
  • FunctionQualifierSpacing: Preserve (default), Space, or NoSpace
  between a function's closing parameter ) and an immediately following
  const or volatile (f() const versus f()const).
  • FunctionRefQualifierSpacing: Preserve (default), Space, or
  NoSpace before a parsed trailing & or && after the parameters
  or a const/volatile qualifier (f() const & versus f() const&).
  • ReturnArrowSpacing: Preserve (default), NoSpace (f()->T), Left
  (f() ->T), Right (f()-> T), or LeftAndRight (f() -> T) around
  the arrow of a parsed trailing return type. Member-access -> is
  unaffected.
  • FunctionParameterClosingParenthesis and
  FunctionCallArgumentClosingParenthesis: SameLine (default, beside the
  final parameter or argument) or NewLine selects the position when
  reflowing a list. Their matching Enforce... options (default false)
  also apply the placement to existing closing gaps without reflow.
  • FunctionParameterSplit: Pack (default, put as many parameters as fit
  on each line) or OnePerLine. EnforceFunctionParameterSplit (default
  false) reflows safe lists that exceed ColumnLimit or are already split.
  Without enforcement, existing parameter lists remain source-owned.
  Lists already fitting on one line are left unchanged.
  • FirstFunctionParameterPosition: NameLine (default), NewLine, or
  NameLineIfFits. The latter keeps the first parameter beside ( only
  if it and its following comma or closing suffix fit within ColumnLimit;
  otherwise it moves the first parameter to a new line. Reflowed lists
  use the configured position, even when they were already split.
  • EnforceFirstFunctionParameterPosition: false (default) preserves the
  first parameter's position in already split lists that need no reflow;
  true applies the configured position to those lists as well.
  • FunctionParameterIndent: IndentWidth (default, indent one width
  beyond the declaration) or Continuation (align with the character
  immediately after the opening ().
  • FunctionParameterNewLineIndent: optional integer from 0 to 32. When
  nonzero and the first parameter starts on a new line, indent all its
  continuation lines by this many columns relative to the declaration,
  instead of aligning them beneath (. Zero (default) uses the normal
  FunctionParameterIndent policy.
  • MergeFunctionParameters: false (default) retains split lists as
  multiline even when they would fit on one line; true joins them when
  the result fits within ColumnLimit.
  • FunctionSpaceBeforeParameters: Preserve (default) leaves the gap
  between a named function declarator and its parameter ( unchanged;
  Space inserts one space, and NoSpace removes it. Applies to function
  declarations and definitions, including constructors, destructors, and
  operators. Newlines, comments, and directives remain unchanged. Calls,
  lambdas, control conditions, and function-pointer declarations are
  unaffected.
  • SpaceInsideFunctionParameters: NoSpace (default) or Space sets
  the edges of reflowed nonempty parameter lists. Set
  EnforceSpaceInsideFunctionParameters (default false) to also
  normalize existing same-line gaps. Newlines, adjacent comments, and
  directives are preserved. Empty () pairs, lambdas, calls, control
  conditions, and requires expressions are unaffected.

Function parameter rules change only safely movable lists in function
declarators; comments, directives, multiline parameter expressions, and
CRLF lists are left intact. They do not change calls.

)HIT_FMT_HELP", HelpExamples4},
    {R"HIT_FMT_HELP(Lambdas)HIT_FMT_HELP", R"HIT_FMT_HELP(  • EmptyLambdaBodySpacing: Inherit (default) uses
  EmptyBlockInteriorSpacing; Preserve, Space, or NoSpace overrides
  it only for empty parsed lambda bodies. YTsaurus uses both {} and
  { }, so its example profile chooses Preserve.
  • SingleStatementLambdaBodyLayout: Inherit (default) uses
  BlockInteriorSpacing for inline lambda bodies; Preserve leaves the
  interior of single-statement lambdas untouched. OneLineIfFits joins a
  safe multiline, single-line statement and its braces if the complete
  resulting line fits ColumnLimit; it uses spaces around the statement.
  Comments, directives, multiline statements, and recovered bodies stay
  source-owned. The YTsaurus profile chooses Preserve because both
  inline and multiline forms appear in the source.
  • LambdaSpaceBeforeParameters: Preserve (default) leaves the gap between a
  lambda's capture ] and immediate parameter ( unchanged; Space
  inserts exactly one space and NoSpace removes it. Newlines and
  intervening comments or directives are left unchanged. Calls and other
  parenthesized expressions are unaffected.
  • SpaceInsideLambdaParameters: Preserve (default) leaves the gaps just
  inside the ( and ) of a nonempty lambda parameter list unchanged;
  Space uses one space and NoSpace removes it. For example, Space
  formats [](x) as []( x ). Applies to generic lambdas as well.
  Empty (), newlines, incomplete lists, and gaps beside directives or
  line comments remain untouched. A trailing block comment before )
  follows this option only when separated by horizontal whitespace; the
  comment text stays unchanged. Does not affect capture brackets, the gap
  before (, named functions, calls, or control conditions.
  • SpaceAroundCaptureList: Preserve (default) leaves the whitespace
  immediately inside the [ and ] of a nonempty lambda capture list
  unchanged; Space uses one space and NoSpace removes it. Applies to
  implicit ([=], [&]) and explicit ([value], [&value]) captures.
  Empty brackets, multiline gaps, adjacent comments or directives, and
  unrelated brackets remain untouched. Comma spacing is not changed.
  • SpaceAroundCaptureAssignment: Preserve (default) leaves whitespace
  on either side of an initialized capture's = unchanged; Space uses
  one space on each same-line side and NoSpace removes it. Standalone
  capture defaults ([=] and [=, x]) are not assignments, nor are =
  tokens inside initializers. Newlines and gaps next to comments or
  directives remain untouched. This setting does not change & spacing.
  • LambdaCaptureSplit: Preserve (default) leaves capture lists as
  written; OnePerLineOnOverflow puts each safe, parsed capture on its
  own line with [ on the header line and ] aligned with that line.
  It triggers only when the capture list itself crosses ColumnLimit,
  not when a short capture list precedes long lambda parameters. Such a
  lambda can still move below ( if its full header then fits. Existing
  multiline lists, comments, directives, and ambiguous captures stay
  source-owned. The YTsaurus profile uses OnePerLineOnOverflow to avoid
  moving a multiline lambda away from its call merely to shorten the
  header.
  • LambdaCallSpaceBeforeArguments: Inherit (default) uses
  FunctionCallSpaceBeforeParameters for an immediately invoked lambda;
  Preserve keeps the original gap, Space inserts one space, and
  NoSpace removes it. Other calls and lambda parameter declarations
  are unaffected.

)HIT_FMT_HELP", HelpExamples5},
    {R"HIT_FMT_HELP(Calls and chains)HIT_FMT_HELP", R"HIT_FMT_HELP(  • EmptyFunctionCallArgumentsSpacing, EmptyFunctionParametersSpacing,
  EmptyLambdaParametersSpacing, and EmptyLambdaCaptureSpacing:
  Preserve (default), Space, or NoSpace inside their respective
  empty, parsed pairs: f( ), void f( ), []( ), and [ ] versus
  f(), void f(), [](), and []. Nonempty interiors have separate
  options. All token-spacing rules leave newlines, comments, directives,
  and incomplete statements unchanged.
  • CallArgumentSplit: Pack (default, put as many arguments as fit on each
  line) or OnePerLine (one argument per continuation line when split,
  including
  safe calls already split in the source). EnforceCallArgumentSplit
  (default false) enables reflow of safe existing lists; without it,
  call lists remain source-owned. Independent of ExpressionLayoutCondition.
  Calls that fit on one line
  are not forced to split. A single safe argument can move to a continuation
  line. For nested single-argument calls, safe one-break inner and outer
  layouts are compared so the chosen split balances the resulting lines.
  With expression layout enabled, safe logical-operator, cast-operand,
  and single-call-argument breaks are compared on overlong lines.
  Fitting outer logical expressions take priority over nested breaks;
  remaining candidates are ranked by maximum width, then balance.
  Ternary layout runs first;
  name-specific call limits and source-owned syntax retain their policies.
  With OnePerLine, an enclosing multi-argument call owns the overflow.
  With FirstCallArgumentPosition = NewLine,
  CallArgumentIndent = IndentWidth, and a same-line closing parenthesis,
  a safe multiline nested-call argument can move with its continuation
  adjusted to the new base. Comments, directives, multiline token text, braced argument bodies,
  and tab indentation remain source-owned.
  A short single-argument member call is not split just because the following
  chain segment overflows; multiline lambda segments stay source-owned.
  A direct multiline lambda can move below ( only if the relocated
  header fits and its body can be reindented safely. When a call split
  resolves an overlong assignment RHS, a safe break after = can join
  if the new first line
  fits; other existing breaks stay as written. Safe two-argument
  static_assert declarations use the same split policy.
  • CallArgumentSplitColumnLimit: 0 (default) uses ColumnLimit for every
  call. With a positive value, calls named in
  CallArgumentSplitColumnLimitNames use the smaller of that value and
  ColumnLimit to decide when to reflow arguments. The names are exact,
  bare call identifiers, default []; other calls keep the global limit.
  This does not change the independent line-length check. The
  YTsaurus profile selects EXPECT_THROW at 90 columns without splitting
  shorter assertions.
  • FirstCallArgumentPosition: CallLine (default) or NewLine selects
  where the first argument starts when a call is split.
  • MultilineSingleCallArgumentPosition: CallLine (default) keeps the
  opening of a multiline sole argument beside the call name; NewLine
  places it below the opening ( using CallArgumentIndent when a split is
  generated.
  EnforceMultilineSingleCallArgumentPosition (default false) additionally
  checks and corrects safe existing multiline calls, even when
  EnforceCallArgumentSplit = false. It does not split flat, fitting arguments or
  reshape the argument's interior. A sole nested call stays on the outer
  call's line so its own arguments can split first. Braced and
  lambda-containing arguments, comments, directives, and recovered syntax
  remain source-owned (apart from safe overlong direct lambdas, whose body
  indentation moves with the capture). Without enforcement it applies when
  CallArgumentSplit is enabled. The YTsaurus profile uses NewLine
  with enforcement disabled; generated multiline arguments still move.
  • ChainedCallRootNames: exact names of call expressions that may start
  a formatted member-call chain, default []. A simple name also matches
  namespace-qualified calls ending in that name (MakeAction matches
  NExample::MakeAction); a fully qualified name can restrict matching
  to that spelling. A similarly named function or a method invoked on an
  object is not a match. An empty list leaves all chains unchanged.
  • ChainedCallSplitCondition: Preserve (default) keeps chains unchanged;
  Overflow splits only if an individual line of the chain exceeds
  ColumnLimit measured from its root, excluding preceding statements
  and surrounding ternary text. Always puts the first eligible
  continuation on its own line regardless of width. Safe existing breaks
  retain their indentation in Overflow mode.
  • ChainedCallLayout: OnePerLine (default) splits each member call once
  the condition applies; Pack places as many calls as fit on a line.
  A non-call receiver keeps its first call on the receiver line. Nested
  calls, comments, directives, CRLF, and multiline call arguments remain
  source-owned when they cannot be reflowed safely.
  • ChainedCallIndent: IndentWidth (default) indents new chain lines one
  level relative to the chain statement. Continuation aligns them with
  the start of the root call. Existing breaks are not reindented by
  Overflow.
  • CallLikeMacroNames: exact macro names whose declaration-like syntax
  the parser treats as abstract function declarators. These opt-in names
  follow FunctionParameterSplit without reflowing unrelated macros.
  • WhitespaceSensitiveMacros: exact macro names, default []. Preserve
  the bytes from each named invocation through its matching ), even
  if other formatting passes reflow or respaced the enclosing statement.
  Other code around the invocation remains eligible for formatting.
  Incomplete or unmatchable invocations keep the source unchanged.
  YTsaurus selects PP_STRINGIZE, the four DEFINE_*ENUM* variants,
  and YT_DEFINE_ERROR_ENUM. This preserves the source placement of
  DEFINE_ENUM's closing ); rather than imposing a new layout.
  • CallLineFirstStringNames: array of exact, quoted call identifier names,
  default []. In a parsed call to one of these identifiers, the first
  complete, single-token string-literal argument stays on the call line
  when the call is split. Earlier arguments remain on that line too,
  even if it exceeds ColumnLimit. Other arguments follow
  CallArgumentSplit. Inter-argument comments, multiline arguments,
  and unsafe calls remain unchanged. The YTsaurus profile selects direct
  logging calls and THROW_ERROR_EXCEPTION, but not _IF or _UNLESS
  variants.
  • EnforceFirstCallArgumentPosition: false (default) preserves the first
  argument's position in already split calls that need no reflow; true
  applies FirstCallArgumentPosition to those calls as well. A reflowed
  call uses the configured position even when it was already split.
  • CallArgumentIndent: IndentWidth (default, indent one width beyond
  the call's line) or Continuation (align with the character after the
  opening (). With expression layout enabled, Continuation also aligns
  new operator breaks inside complete parsed calls, including single-
  argument macros such as assert. Call-list reflow only affects calls
  with plain, single-line arguments and whitespace-only list gaps.
  Single-line /*...*/ comments inside a parameter move with that
  parameter. Comments between arguments, directives, CRLF, and ambiguous
  parenthesized callees remain unchanged. Parsed function-like
  macro calls follow the same call layout rules; nested calls are considered
  after enclosing calls, so outer commas can resolve overflow before inner
  commas are tried. Multiline arguments keep their call list unchanged.
  Reflowed lists use comma-space separators;
  SpaceInsideCallArguments = Space also applies to their same-line edges.
  • FunctionCallSpaceBeforeParameters: Preserve (default) leaves the gap
  between a parsed call's callee and argument ( unchanged; Space
  inserts one space and NoSpace removes it. Newlines, intervening
  comments, and directives are left unchanged. Function declarations,
  lambda parameters, built-in type casts, and control or requires
  expressions are unaffected. Ambiguous parenthesized callees, which
  could instead be C-style casts, are preserved unless the inner expression
  is clearly callable (such as (*fn)(arg)). Calls in incompletely parsed
  statements remain source-owned. Parentheses inside a templated return
  type (including member-pointer types) are not mistaken for calls. The
  setting controls the gap *before* the argument list, not whitespace
  between individual arguments.
  • SpaceInsideCallArguments: NoSpace (default) or Space selects
  the edges of reflowed nonempty call argument lists. Set
  EnforceSpaceInsideCallArguments (default false) to also normalize
  existing same-line gaps. Empty pairs,
  newlines, and gaps adjacent to comments or directives remain untouched.
  Ambiguous parenthesized callees (which may be casts), built-in type
  casts, declarations, lambda parameters, control conditions, and calls
  in incompletely parsed statements are unaffected. The rule does not
  change whitespace between arguments.

)HIT_FMT_HELP", HelpExamples6},
    {R"HIT_FMT_HELP(Control flow)HIT_FMT_HELP", R"HIT_FMT_HELP(  • ControlBlockConditionSpacing: Preserve (default), Space, or
  NoSpace just inside complete if, for, while, switch, and catch
  condition parentheses (including do/while).
  ControlBlockKeywordSpacing controls the gap *before* (.
  • ControlBlockSpaceBeforeBrace: Space (default) or NoSpace before
  a new brace; EnforceControlBlockSpaceBeforeBrace (default false)
  also normalizes existing same-line gaps between a control header
  (if, else, for, while, do, switch, try, catch) and a
  same-line opening brace. Brace placement
  runs first; a brace left on a separate line stays on that line.
  • LoopSemicolonSpacing and ConditionInitSemicolonSpacing: Preserve
  (default), Space, or NoSpace. The first controls the two clause
  separators of a parsed for header; the second controls the single
  top-level init-statement separator in a complete if or switch
  condition. When enabled, remove same-line space before the semicolon
  and use the selected spacing after it; no space is added before ) or
  another semicolon. Statement and nested-expression semicolons are not
  changed.
  • AddMissingControlBraces: false (default) or true wraps complete,
  unbraced if/else, for, while, switch, and do bodies in {}.
  Unbraced switch case labels remain unchanged. It handles nested controls
  and else if without changing the binding of else.
  Comments, directives, recovered statements and CRLF gaps remain unchanged.
  Braces follow ControlBlockBracePlacement = AttachUnlessMultiline for
  multiline headers; inserted bodies use IndentWidth. A null-statement
  body (;) is governed separately by EmptyControlBody and is never
  wrapped into { ; }.
  • EmptyControlBody: Preserve (default) keeps an unbraced ; body as
  written even if AddMissingControlBraces = true; EmptyBlock replaces
  the null statement with an empty {} body, independently of that option.
  EmptyBlockInteriorSpacing = Space produces { }. Brace placement
  follows ControlBlockBracePlacement; comments and incomplete or CRLF
  statements stay source-owned.
  • RemoveNullStatements: false (default) retains standalone ;
  statements. true removes complete null statements directly inside a
  compound block, including a redundant ; after switch (...) { ... }.
  Controlled if/while/for bodies, labeled statements, directives,
  comments on the same line and recovered syntax are left alone. This is
  independent of EmptyControlBody.
  • RemoveNullDeclarations: false (default) retains empty declarations
  (;). true removes standalone empty declarations at file scope and in
  namespace, extern "C" and export blocks, including ; after a closing
  brace, and redundant ; after function definitions. Other declaration
  terminators, same-line comments, directives and recovered syntax remain
  untouched. Independent of RemoveNullStatements.
  • IndentCaseLabels: Preserve (default), NoIndent (align labels with
  their switch header), or Indent (CaseLabelIndentWidth inside the switch).
  Statements following a case get one additional level with Indent when
  IndentBlockBodies is enabled. Only parsed case/default labels directly
  in complete switch blocks are affected. An explicit { following a case
  aligns with the case label. This is independent of LabelColonSpacing.
  • ControlHeaderClauseSplit: Preserve (default) leaves control header
  clauses as written; WhenNeeded breaks after both top-level for header
  semicolons or the if/switch init-statement semicolon when the header
  exceeds ColumnLimit or is already multiline. A nested semicolon is not
  a clause separator. Empty clauses, comments, directives, and incomplete
  headers and existing clause breaks remain source-owned.
  if/switch clause breaks use
  ControlConditionIndent; for breaks use ForHeaderClauseIndent.
  YTsaurus selects WhenNeeded, with if/switch clauses indented by
  IndentWidth and for clauses aligned just after (.
  • ForHeaderClauseIndent: Inherit (default) uses
  ControlConditionIndent for for clause breaks, preserving the previous
  setting. IndentWidth indents one level beyond the for statement;
  Continuation aligns under the first character after (. It applies
  only to semicolon breaks selected by ControlHeaderClauseSplit.
  • ControlConditionIndent: IndentWidth (default) indents new expression
  breaks by one width beyond the original line; Continuation aligns
  breaks inside complete parsed if, for, while, switch, and catch
  conditions below the character after the opening (. Header clause
  splitting uses this setting independently of expression layout (for
  headers inherit it unless ForHeaderClauseIndent overrides it); other
  expression breaks require expression layout. Existing expression breaks
  stay source-owned in Overflow mode;
  tabs and recovered headers retain safe fallback layout.
  • ControlBlockKeywordSpacing: Preserve (default) leaves whitespace
  between a control keyword and its condition ( unchanged; Space uses
  exactly one space and NoSpace removes it. Applies to if, for,
  while (including do/while), switch, and catch, as well as
  if constexpr ( and for co_await (. Newlines, comments, directives,
  macro-wrapped conditions, calls, and function declarations are unchanged.
  else and do have no condition ( to which this rule applies.
  • ControlBlockBracePlacement: SameLine (default) attaches generated
  braces to the header; NewLine puts them on a separate line.
  AttachUnlessMultiline separates them when the header spans multiple
  lines (not merely because the brace was detached).
  EnforceControlBlockBracePlacement (default false) also corrects
  existing braces, including after header reflow. Applies
  to compound bodies of if, else, for, while, do, switch, try,
  and catch, but not unbraced statements or unrelated compound blocks.
  Existing same-line whitespace is preserved; attaching a detached brace
  uses one space. Comments and directives that cannot be moved safely stay
  in place.

)HIT_FMT_HELP", HelpExamples7},
    {R"HIT_FMT_HELP(Blocks and statements)HIT_FMT_HELP", R"HIT_FMT_HELP(  • BlockInteriorSpacing: Preserve (default), Space, or NoSpace
  immediately inside a nonempty same-line compound or lambda body
  ({ statement; } versus {statement;}). A complete braced call
  argument with a top-level statement semicolon is classified as a block,
  not an initializer; its multiline indentation remains source-owned. The
  rule does not change line breaks, record/namespace braces, or
  initializer-brace spacing.
  • EmptyBlockInteriorSpacing: NoSpace ({}, default) or Space ({ })
  selects spacing in newly generated empty blocks. Set
  EnforceEmptyBlockInteriorSpacing (default false) to normalize
  existing empty compound and lambda bodies. It is independent
  of the nonempty block setting.
  • IndentBlockBodies: false (default) preserves statement indentation;
  true reindents statement starts and closing braces in complete compound
  and lambda blocks by BlockBodyIndentWidth relative to the header. It also
  indents parsed unbraced control bodies one level beyond the control header.
  Inline statements, continuations, comments, directives, and recovered
  bodies remain source-owned. Unbraced switch case labels and their following
  statements retain their original indentation because they have no
  compound body to anchor case indentation.
  • IndentLeadingStatementComments: false (default) preserves comment
  indentation. When enabled with IndentBlockBodies, standalone //
  comments directly adjacent to a reindented statement follow it if they
  share its old indentation. Blank-separated, trailing, block, and
  directive comments remain source-owned.
  • IndentedCallScopePairs: optional Begin:End call pairs (for example
  ["OnBeginList:OnEndList"]). With IndentBlockBodies = true, preserve
  one existing extra indentation level between matching argument-free .
  or -> calls on the same receiver, or between standalone wrapper calls
  containing one configured, argument-free Begin()/End() argument.
  The method argument may occur anywhere in the wrapper's argument list.
  The wrapper and all other argument tokens must match between the pair;
  only the configured method name may differ. Nested configured scopes may
  add further levels when already indented; flat nested calls do not force
  an extra level. The opening and closing calls must match their
  scope's indentation; intervening calls and declarations must be
  consistently indented. The option never adds an extra level; other
  over-indentation is still corrected.
  • StandaloneBlockBraceIndentation: Indent (default) aligns an anonymous
  scope's opening { with other statements at the same nesting level when
  IndentBlockBodies = true; Preserve leaves only that opening brace's
  existing indentation untouched. The scope body and closing } still
  follow their parsed nesting depth. Control, function, and case braces are
  unaffected.
  • NonEmptyBlockBodyLayout: Preserve (default) leaves one-line
  compound bodies as written; Multiline separates complete parsed
  statements and their braces onto their own lines. It applies to
  functions and control blocks, not lambdas or empty bodies. Comments,
  directives, recovered syntax, and CRLF gaps stay source-owned. The
  YTsaurus profile uses Multiline; its empty functions still use { }
  on a separate line.
  • CollapseEmptyFunctionBodies: false (default) or true turns a complete
  function body containing only whitespace into { }. It does not collapse
  bodies containing comments or directives, or CRLF bodies. Function brace
  placement still controls whether the opening brace is on its own line.
  • BlockClosingBrace: NewLine (default) or SameLine (beside the last
  statement) selects the preferred position. EnforceBlockClosingBrace
  (default false) also corrects existing closers. Applies to parsed
  compound statements and lambda bodies, not namespace or type
  declarations. Empty bodies and unsafe gaps remain unchanged.
  For all closing-placement options, comments and directives adjacent to
  the closer and CRLF gaps remain source-owned. Unrecognized constructs
  other than the restricted typed-initializer closing gap stay unchanged.
  • StatementAttributeIndent: Preserve (default) leaves standalone
  [[...]] statement attributes at their existing indentation, even when
  block body indentation is enabled. Block aligns them with the containing
  block's statement indent (or the control header for an unbraced body);
  Indent adds StatementAttributeIndentWidth to that indent. Only parsed, line-leading
  attributes in complete blocks and control bodies are affected. Inline
  attributes, comments, directives, and conditional branches stay unchanged.
  • StatementSemicolonSpacing: Preserve (default) or NoSpace before
  the terminator of a complete parsed statement (return x ; becomes
  return x;). Loop and condition-init separators have separate options;
  declarations at file scope are not statement terminators.
  • DeclarationSemicolonSpacing: Preserve (default), Space, or
  NoSpace *before* a parsed declaration's terminating semicolon, both
  at file scope and in declaration statements. If both it and
  StatementSemicolonSpacing are enabled, this setting takes precedence
  for declaration statements. It does not change the gap after ; or
  the separators in control headers. Comments, directives, and
  multiline gaps remain unchanged.

)HIT_FMT_HELP", HelpExamples8},
    {R"HIT_FMT_HELP(Initializers, new, and delete)HIT_FMT_HELP", R"HIT_FMT_HELP(  • BraceInitializerSpacing: Preserve (default), Space, or NoSpace
  before a parsed braced initializer (T x {1} versus T x{1}). The gap
  following = remains controlled by AssignmentOperatorSpacing.
  The gap after return/co_return stays source-owned.
  • InitializerBracesSpacing: NoSpace (default) or Space selects
  the edges of reflowed nonempty parsed braced initializers ({ x, y } versus
  {x, y}). EnforceInitializerBracesSpacing (default false) also
  normalizes existing same-line gaps.
  EmptyBraceInitializerInteriorSpacing controls { } versus {}
  for empty parsed braced initializers. Neither setting
  affects block, namespace, record, or lambda braces.
  • InitializerListSplit: Pack (default, put as many braced initializer
  elements on a line as fit), or OnePerLine (one element per continuation
  line when a split is needed).
  Safe overflowing lists reflow using that layout, while lists whose
  physical lines already fit stay unchanged. EnforceInitializerListSplit
  (default false) additionally normalizes safe, already-split lists
  whose lines fit ColumnLimit. Applies only to complete
  parsed braced initializer lists with single-line elements, including
  .field = value and [index] = value designators, comma separators,
  and whitespace-only gaps. Trailing commas are preserved by default;
  comments, directives, CRLF, and multiline elements stay source-owned.
  Lists that already fit on one line are not split. Nested lists are
  reparsed until stable, with a limit of 16 passes; deeper layouts remain
  source-owned rather than leaving a partially formatted result.
  • FirstInitializerElementPosition: BraceLine (default) or NewLine
  determines where the first element starts when a list is split.
  • InitializerListIndent: IndentWidth (default, one width beyond the
  list's line) or Continuation (align with the character after {).
  • InitializerListClosingBrace: SameLine, NewLine, or
  SameLineUnlessTrailingComma (default) chooses the position of }.
  EnforceInitializerListClosingBrace (default false) also applies the
  chosen placement to safe existing gaps without list reflow.
  A multiline final element retains its own closing-brace line; a brace
  anchoring the next sibling initializer stays on its original line.
  Compact comma-terminated lists are split one element per line when
  enforcing a NewLine brace. Unknown typed temporary initializers only
  have their safe closing gap adjusted; their contents are not reflowed.
  • InitializerListTrailingComma: Keep (default) retains its presence on
  reflow, Add inserts one, and Remove drops one.
  EnforceInitializerListTrailingComma
  (default false) additionally applies Add or Remove to safe existing
  parsed lists independently of brace placement. An unknown typed temporary
  can have a safe trailing comma removed, but one is never added to its
  unparsed contents. Gaps next to comments or directives stay unchanged.
  • MergeInitializerList: false (default) leaves already split lists
  multiline; true joins safe lists that fit within ColumnLimit when
  EnforceInitializerListSplit is true, unless the selected closing-brace
  position (with or without a trailing comma) is NewLine.
  • PlacementNewSpaceBeforeArguments: Preserve (default), Space, or
  NoSpace between new and a parsed placement argument list:
  new (buffer) T versus new(buffer) T. Ordinary new T(...) and
  incomplete expressions remain unchanged. Multiline and commented
  gaps are preserved.
  • PlacementNewArgumentInteriorSpacing: Preserve (default), Space,
  or NoSpace just inside nonempty parsed placement arguments:
  new ( buffer ) T versus new (buffer) T.
  • PlacementNewSpaceAfterArguments: Preserve (default), Space, or
  NoSpace between a complete placement argument list and the new type:
  new(buffer) T versus new(buffer)T. The before-argument gap is
  controlled by PlacementNewSpaceBeforeArguments. Multiline and
  commented gaps, missing types, and missing operands are preserved.
  • NewArraySpaceBeforeBrackets and NewArrayInteriorSpacing:
  Preserve (default), Space, or NoSpace before a parsed array
  bracket in a new type-id (new T [4] versus new T[4]) and just
  inside its nonempty brackets (new T[ 4 ] versus new T[4]),
  respectively. The parser treats new T*[4] as an array of pointers,
  not multiplication; the bracket gap follows this array setting. Nested
  template arguments and attributes are unaffected.
  • EmptyArrayDeclarationInteriorSpacing and
  EmptyNewArrayInteriorSpacing: Preserve (default), Space, or
  NoSpace inside empty brackets in array declarations (int a[ ]
  versus int a[]) and new type-ids (new T[ ]{1} versus
  new T[]{1}), respectively. Nonempty brackets use their own rules.
  • NewInitializerSpaceBeforeParentheses: Preserve (default), Space,
  or NoSpace between the type and parsed initializer parentheses in
  new T (1) versus new T(1). Placement arguments and braced
  initializers have separate rules.
  • NewInitializerParenthesisInteriorSpacing: Preserve (default),
  Space, or NoSpace just inside parsed new initializer parentheses,
  including both new T( 1 ) versus new T(1) and new T( ) versus
  new T(). Placement arguments and braced initializers are unaffected.
  • NewInitializerSpaceBeforeBraces: Preserve (default), Space, or
  NoSpace between the type and a parsed new initializer {:
  new T {1} versus new T{1}.
  • NewInitializerBraceInteriorSpacing: Preserve (default), Space,
  or NoSpace just inside parsed new braced initializers, both
  nonempty (new T{ 1 } versus new T{1}) and empty (new T{ }
  versus new T{}). The gap before { has a separate option.
  • DeleteArraySpaceBeforeBrackets: Preserve (default), Space, or
  NoSpace between delete and the complete array marker in
  delete [] p versus delete[] p. Ordinary delete p is unchanged.
  • DeleteArraySpaceAfterBrackets: Preserve (default), Space, or
  NoSpace between a complete delete[] marker and its operand:
  delete[] p versus delete[]p. The before-bracket gap is controlled
  by DeleteArraySpaceBeforeBrackets.
  • ConstructorInitializerList: Preserve (default) or Format for
  complete constructor definitions. Format enables the same layout
  controls as InheritanceList, using ConstructorInitializerList*
  names. ConstructorInitializerListColonPlacement is
  AttachToInitializer (default) or AttachToConstructor;
  ConstructorInitializerListStart is NewLine (default),
  ConstructorLine, or NewLineOnOverflow.
  ConstructorInitializerListSplit is OnePerLine (default), Pack,
  or OnePerLineOnOverflow. Colon and comma spacing use independent
  *SpaceBefore and *SpaceAfter options (NoSpace or Space,
  defaulting to NoSpace before and Space after). Comma placement is
  AttachToNext (default) or AttachToPrevious.
  ConstructorInitializerListIndent is an optional positive integer up
  to 32, defaulting to IndentWidth; ColumnLimit controls packing and
  overflow. Only complete single-line initializer entries with
  whitespace-only separators are movable. Comments, directives,
  recovered syntax, and multiline entries remain source-owned.

)HIT_FMT_HELP", HelpExamples9},
    {R"HIT_FMT_HELP(Expressions and operators)HIT_FMT_HELP", R"HIT_FMT_HELP(  • NamedCastSpaceBeforeTypeArguments: Preserve (default), Space, or
  NoSpace between static_cast, const_cast, reinterpret_cast, or
  dynamic_cast and the < of a complete parsed cast
  (static_cast <int>(x) versus static_cast<int>(x)). Ambiguous or
  >>-terminated type arguments remain unchanged.
  • NamedCastSpaceBeforeOperandParentheses: Preserve (default),
  Space, or NoSpace between the closing > and operand ( of a
  complete parsed named cast (static_cast<int> (x) versus
  static_cast<int>(x)). Ambiguous >>-terminated type arguments
  remain source-owned.
  • ReturnSpaceBeforeParentheses, ThrowSpaceBeforeParentheses, and
  CoReturnSpaceBeforeParentheses: Preserve (default), Space, or
  NoSpace between the keyword and a complete grouped operand:
  return (x) versus return(x), similarly for throw and
  co_return. They do not change other expression operands.
  • MemberAccessOperatorsSpacing: Preserve (default) or NoSpace
  around parsed . and -> access (obj . field becomes obj.field)
  and unambiguous .* and ->* tokens. :: is independent.
  • ScopeResolutionSpacing: Preserve (default) or NoSpace around parsed
  qualified :: names (std :: vector becomes std::vector). Leading
  :: keeps its left gap. Prefer Preserve when declaration-prefix macros
  may occur: without name lookup or macro expansion, GTEST_API_ ::std
  cannot reliably be distinguished from a qualified name.
  • NoexceptSpaceBeforeParentheses, AlignasSpaceBeforeParentheses,
  AlignofSpaceBeforeParentheses, SizeofSpaceBeforeParentheses,
  DecltypeSpaceBeforeParentheses, TypeidSpaceBeforeParentheses,
  RequiresSpaceBeforeParentheses, and
  StaticAssertSpaceBeforeParentheses: Preserve (default), Space, or
  NoSpace between the respective keyword and an immediately following
  parsed (, for example noexcept (x) versus noexcept(x).
  noexcept expressions and function specifiers share one setting;
  requires expressions and clauses share another. alignas applies
  to parsed record headers and standalone declarations. sizeof...(Ts)
  is unaffected. Other unparsed syntax, incomplete pairs, comments, and
  newlines are preserved.
  • SizeofPackSpaceBeforeParentheses: Preserve (default), Space, or
  NoSpace between sizeof... and its parsed pack-argument ( in
  sizeof... (Ts) versus sizeof...(Ts). Ordinary sizeof(x) has
  its own setting. Multiline and commented gaps are preserved.
  • CoAwaitSpaceBeforeParentheses: Preserve (default), Space, or
  NoSpace between co_await and a complete grouped operand:
  co_await (expr) versus co_await(expr). Other operands are
  unaffected. Multiline and commented gaps are preserved.
  • OperatorOverloadOperatorSpacing: Preserve (default), NoSpace,
  Left, Right, or LeftAndRight around the symbolic operator in a
  parsed overload name: operator+() versus operator + (). Left
  controls the gap after operator, and Right the gap before the
  function parameters. Conversion operators (operator int) are
  untouched to avoid merging keywords.
  • CastArgumentSpacing: Preserve (default), Space, or NoSpace just
  inside the parentheses of a complete parsed C-style or named cast:
  ( int )value and static_cast<int>( value ). Empty, multiline, and
  commented gaps are unchanged. CastSpacing independently controls the
  space after a C-style cast.
  • GroupingInteriorSpacing: Preserve (default), Space, or NoSpace
  immediately inside nonempty parsed expression grouping parentheses:
  ( a + b ) versus (a + b). Calls, declarators, and casts use their
  own spacing rules.
  • UnaryOperatorSpacing and PostfixOperatorSpacing: Preserve
  (default) or NoSpace. UnaryOperatorSpacing removes same-line whitespace
  between a parsed symbolic prefix operator and its operand (! x becomes
  !x); PostfixOperatorSpacing removes space before a parsed postfix ++ or
  -- (x ++ becomes x++). Word operators (sizeof, alignof, co_await) retain
  their required whitespace. Gaps that could merge two operators remain
  untouched.
  • TernaryOperatorSpacing: Preserve (default) or Space on both
  same-line sides of parsed ? and : (a?b:c becomes a ? b : c).
  This is independent of TernaryOperatorPlacement (line breaks).
  • BinaryOperatorSpacing and AssignmentOperatorSpacing: Preserve
  (default), Space, or NoSpace on both sides of parsed binary and
  assignment operators. Commas use CommaSpacing independently, even when
  that option is Preserve. Assignment initializers are included. Operators
  that could also be template delimiters (<, >, <<, >>) remain
  unchanged. An operator with no valid operand is not treated as binary;
  for example char in an otherwise ambiguous call stays source-owned.
  NoSpace does not join two operator spellings if that could change
  tokenization. In a fold expression, GroupingInteriorSpacing owns the
  gap between the trailing ... and ).
  • CastSpacing: Preserve (default), Space, or NoSpace between the
  closing ) and operand of a parsed C-style cast. Named casts and
  parenthesized operands of sizeof, alignof, decltype, typeid, and
  noexcept are not affected.
  • CommaSpacing: Space (default, also spelled After) separates
  reflowed list elements; NoSpace joins them without a gap. Set
  EnforceCommaSpacing (default false) to also normalize existing
  same-line comma gaps (f(a ,b) becomes f(a, b) under Space or
  f(a,b) under NoSpace). A trailing comma before a closing delimiter does not receive
  an added space. Comments, line breaks, and recovered syntax are preserved.
  • SubscriptOperatorInteriorSpacing, ArrayDeclarationInteriorSpacing,
  and StructuredBindingBracketSpacing: Preserve (default), Space, or
  NoSpace just inside nonempty parsed brackets of their respective
  constructs: a[ i ], int a[ 4 ], and auto [ x, y ]. Lambda captures
  and attributes have separate rules or remain untouched.
  • SubscriptOperatorSpacing and ArrayDeclarationSpacing: Preserve
  (default), Space, or NoSpace before the respective opening bracket:
  a [i] versus a[i], and int a [4] versus int a[4]. These settings
  are independent of their interior-spacing counterparts.
  • ExpressionLayoutCondition: Preserve (default) retains expression
  and call argument layout. Overflow splits safe overlong lines without
  joining existing breaks. FitOrOverflow also joins safe multiline
  expressions and calls when their flat form fits within ColumnLimit;
  otherwise it behaves like Overflow. Always starts from the flat form
  and reflows it according to ColumnLimit, even when that flat form
  overflows. Always is not recommended for stable formatting: formatter
  updates or small nearby edits can cause widespread layout changes.
  Expression operator layout covers parsed binary, assignment, and
  conditional operators. Operator gaps are normalized in Always mode;
  other expression gaps, including call argument breaks, are controlled
  separately by CallArgumentSplit. A fitting outer break is preferred to
  breaks inside calls, parentheses, or subscripts; inner breaks remain
  available when no outer break fits. New continuations normally use
  IndentWidth beyond the original line's leading spaces; parsed control
  conditions can instead use ControlConditionIndent, and parsed calls
  can use CallArgumentIndent. Comments,
  directives, CRLF, backslashes, and ambiguous or recovered syntax stay
  source-owned. Bare <, >, and >> remain untouched when they could be
  template syntax; parsed << shifts can be split.
  • DefaultBinaryOperatorPlacement: Trailing (default, keep a wrapped
  binary operator at the end of the previous line) or Leading (put it at
  the start of the continuation line). BinaryOperatorPlacement remains
  a compatibility alias for this setting. TrailingBinaryOperatorList and
  LeadingBinaryOperatorList override the default for exact operator
  spellings (for example ["+", "<<"] or ["||"]). An operator cannot
  occur in both lists. Only parsed binary operators are affected; unary,
  assignment, and ternary operators use their own settings. These options
  apply when ExpressionLayoutCondition is not Preserve. Overflow keeps
  existing operator breaks but safely moves the newline to the selected
  side, even if the expression otherwise fits on one line.
  • AssignmentOperatorPlacement: Leading (default, put a wrapped
  assignment operator at the start of the continuation line) or Trailing
  (keep it at the end of the preceding line). Like binary placement, this
  only applies to parsed expressions when expression layout is enabled;
  Overflow also corrects safe existing assignment breaks without
  regrouping the expression.
  • TernaryOperatorPlacement: Leading (default) puts newly created
  ? and : breaks at the start of continuation lines; Trailing
  puts them at the end of the preceding line. Existing breaks retain
  their side unless EnforceTernaryOperatorPlacement is true (default
  false). Enforcement also corrects safe existing breaks in Overflow
  mode, independently of TernarySplit.
  • TernarySplit: AsNeeded (default) allows either delimiter or both to
  break. EnforceTernarySplit (default false) enables expression reflow
  to correct safe ternary breaks even without another expression layout
  setting. ThreeLines
  keeps a flat ternary flat when it fits; if the ternary is split, it
  separates the condition, true branch, and false branch by breaking at
  both delimiters. Safe overlong ternaries are considered before call
  argument reflow so a single argument does not hide the ternary break.
  Safe leading ThreeLines breaks align ? and : at
  least one indent level past the condition's line, including when a
  trailing-operator break is moved to the leading side. These settings
  apply when ExpressionLayoutCondition is not Preserve, or when
  EnforceTernarySplit is true (using Overflow as the fallback).
  Comments, directives, CRLF, and ambiguous or incomplete
  syntax stay source-owned. In --check mode, expression reflow is
  skipped; safe call and parameter reflow still produces suggested fixes.
  Unresolved layout checks report overlong lines, incorrect existing
  operator placement, and incomplete ThreeLines splits.

)HIT_FMT_HELP", HelpExamples10},
    {R"HIT_FMT_HELP(Naming checks)HIT_FMT_HELP", R"HIT_FMT_HELP(  • <Kind>NameCase and <Kind>NamePrefixes: independent naming checks for
  Class, Enum, Union, Struct, Function, Concept, Variable, and
  Namespace. Cases are camelCase, PascalCase, snake_case, and
  SCREAMING_SNAKE_CASE (ASCII letters and digits), or Unchecked to disable
  the case check. Prefixes are an array of quoted identifier fragments;
  at least one must match. The case applies to the name *after* its prefix.
  Use [] to disable the prefix check or [""] to allow any prefix.
  Either check can be enabled independently; omitted checks are disabled. Only unambiguous definitions and variable declarations are
  checked; forward declarations, function prototypes, explicit template
  instantiations, references, aliases, macros and uncertain declarators are
  skipped. Names in function definitions, including parameter names, are
  checked. --fix leaves names untouched.
  • NamingNamespaceExemptions: qualified namespace paths excluded from all
  naming checks, including the namespace name and everything it contains.
  Defaults to ["std"], which also excludes std::chrono. Use [] to
  disable exemptions or list other paths such as "vendor::api".
  Matching uses complete path components (so std does not exclude
  standard). These exemptions do not affect formatting or the independent
  unnamed-function-parameter checks.
  • FunctionDeclarationParameterNames: Unchecked (default) or
  Required. When required, check mode reports unnamed parameters in
  unambiguous function declarations; a comment does not count as a name.
  • FunctionDefinitionParameterNames: Unchecked (default),
  NameRequired, or NameOrCommentedNameRequired. The last option also
  permits a trailing identifier comment such as int /*unused*/.
  Neither check renames parameters or alters formatting output.

)HIT_FMT_HELP", HelpExamples11},
    {R"HIT_FMT_HELP(Path exclusions)HIT_FMT_HELP", R"HIT_FMT_HELP(  • Exclude: ECMAScript regular expressions searched against paths relative
  to the config directory. A matching directory and its contents are skipped
  during recursive processing. An explicit file input is skipped if it
  matches. --no-config disables these exclusions; repeatable CLI
  --exclude patterns apply relative to each input directory instead.
  Backslashes in patterns must be doubled inside quoted config strings.

)HIT_FMT_HELP", HelpExamples12},
};
// hit-fmt on

} // namespace hit_fmt
