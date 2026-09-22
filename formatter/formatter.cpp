#include "formatter.hpp"

#include <tokenizer/token.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace formatter {
namespace {

using NK = parser::NodeKind;
using PR = parser::PunctuationRole;
using TK = tokenizer::TokenKind;
using TokenIndex = parser::TokenIndex;

constexpr TokenIndex NoToken = std::numeric_limits<TokenIndex>::max();

enum class BraceKind : uint8_t
{
    None,
    Block,
    CaseBlock,
    Function,
    ConstructorFunction,
    Record,
    Namespace,
    UnindentedBlock,
    Enum,
    Initializer,
    Lambda,
    Requirement,
};

bool IsOpening(TK kind)
{
    return kind == TK::Operator_OpenParenthesis ||
        kind == TK::Operator_OpenBracket || kind == TK::Operator_OpenBrace;
}

bool IsClosing(TK kind)
{
    return kind == TK::Operator_CloseParenthesis ||
        kind == TK::Operator_CloseBracket || kind == TK::Operator_CloseBrace;
}

bool IsMemberAccess(TK kind)
{
    return kind == TK::Operator_ClassMemberAccess ||
        kind == TK::Operator_IndirectClassMemberAccess;
}

bool IsControlKeyword(TK kind)
{
    switch (kind) {
    case TK::Keyword_if:
    case TK::Keyword_for:
    case TK::Keyword_while:
    case TK::Keyword_switch:
    case TK::Keyword_catch:
        return true;
    default:
        return false;
    }
}

bool IsWord(TK kind)
{
    return kind == TK::Identifier || (kind >= TK::Literal_UserDefinedInteger &&
        kind <= TK::Literal_String) || (kind >= TK::Keyword_alignas && kind <= TK::Keyword_while);
}

bool IsUppercaseMacroName(std::string_view text)
{
    if (text.empty()) {
        return false;
    }
    return std::ranges::all_of(text, [] (char character) {
        auto value = static_cast<unsigned char>(character);
        return character == '_' || std::isdigit(value) || std::isupper(value);
    });
}

bool IsBinaryToken(TK kind)
{
    switch (kind) {
    case TK::Operator_Assign:
    case TK::Operator_PlusAssign:
    case TK::Operator_MinusAssign:
    case TK::Operator_MultiplyAssign:
    case TK::Operator_DivideAssign:
    case TK::Operator_RemainderAssign:
    case TK::Operator_XorAssign:
    case TK::Operator_BitAndAssign:
    case TK::Operator_BitOrAssign:
    case TK::Operator_Equals:
    case TK::Operator_NotEquals:
    case TK::Operator_LessEquals:
    case TK::Operator_GreaterEquals:
    case TK::Operator_Spaceship:
    case TK::Operator_And:
    case TK::Operator_Or:
    case TK::Operator_LeftShift:
    case TK::Operator_RightShift:
    case TK::Operator_LeftShiftAssign:
    case TK::Operator_RightShiftAssign:
    case TK::Operator_Conditional:
        return true;
    default:
        return false;
    }
}

bool IsAssignmentToken(TK kind)
{
    switch (kind) {
    case TK::Operator_Assign:
    case TK::Operator_PlusAssign:
    case TK::Operator_MinusAssign:
    case TK::Operator_MultiplyAssign:
    case TK::Operator_DivideAssign:
    case TK::Operator_RemainderAssign:
    case TK::Operator_XorAssign:
    case TK::Operator_BitAndAssign:
    case TK::Operator_BitOrAssign:
    case TK::Operator_LeftShiftAssign:
    case TK::Operator_RightShiftAssign:
        return true;
    default:
        return false;
    }
}

bool IsContextDependentOperator(TK kind)
{
    return kind == TK::Operator_Star || kind == TK::Operator_Ampersand ||
        kind == TK::Operator_And || kind == TK::Operator_Plus ||
        kind == TK::Operator_Minus || kind == TK::Operator_Less ||
        kind == TK::Operator_Greater || kind == TK::Operator_LeftShift ||
        kind == TK::Operator_RightShift ||
        kind == TK::Operator_IndirectClassMemberAccess;
}

bool IsBinaryRole(PR role)
{
    return role == PR::BinaryOperator || role == PR::AssignmentOperator ||
        role == PR::ComparisonOperator || role == PR::ShiftOperator ||
        role == PR::ConditionalDelimiter;
}

bool IsBlockBrace(BraceKind kind)
{
    return kind == BraceKind::Block || kind == BraceKind::CaseBlock ||
        kind == BraceKind::Function ||
        kind == BraceKind::ConstructorFunction || kind == BraceKind::Record ||
        kind == BraceKind::Namespace || kind == BraceKind::UnindentedBlock ||
        kind == BraceKind::Enum || kind == BraceKind::Lambda ||
        kind == BraceKind::Requirement;
}

class Formatter
{
    struct TokenFlags
    {
        uint32_t AngleDelimiter : 1 = false;
        uint32_t SpacedColon : 1 = false;
        uint32_t DefinitionStart : 1 = false;
        uint32_t BreakAfter : 1 = false;
        uint32_t PreserveBreakBefore : 1 = false;
        uint32_t BaseSeparator : 1 = false;
        uint32_t ConstructorSeparator : 1 = false;
        uint32_t CompactLambda : 1 = false;
        uint32_t StructuredBinding : 1 = false;
        uint32_t LambdaDelimiter : 1 = false;
        uint32_t LambdaReturnArrow : 1 = false;
        uint32_t DeclaratorOperator : 1 = false;
        uint32_t SpaceBefore : 1 = false;
        uint32_t PreserveLineIndent : 1 = false;
        uint32_t DoWhileContinuation : 1 = false;
        uint32_t InlineRecord : 1 = false;
        uint32_t TemplateParameterPack : 1 = false;
        uint32_t IndentedCaseLabel : 1 = false;
        uint32_t BeginIndentedCaseBody : 1 = false;
        uint32_t EndIndentedCaseBody : 1 = false;
        uint32_t CaseLabelDelimiter : 1 = false;
    };

    struct LambdaInfo
    {
        // Body-opening brace -> capture-opening bracket.
        TokenIndex Capture = NoToken;
        // Output indentation at a capture-opening bracket.
        TokenIndex Indent = NoToken;
    };

    struct OutputPosition
    {
        TokenIndex Line = NoToken;
        TokenIndex Column = NoToken;
    };

    struct Layout
    {
        TokenIndex Opening;
        TokenIndex Closing;
        bool Multiline;
        bool BreakElements;
        bool ClosingOwnLine;
        bool Indents;
        bool Regroup;
        std::ptrdiff_t IndentOffset;
        size_t OpeningLineIndent;
    };

    struct NamespaceComment
    {
        TokenIndex Closing;
        std::string Text;
    };

    enum class BraceInsertionKind : uint8_t
    {
        Close,
        Open,
    };

    struct BraceInsertion
    {
        TokenIndex Index;
        BraceInsertionKind Kind;
    };

    class DelimiterPairs
    {
    public:
        explicit DelimiterPairs(const parser::SyntaxTree& tree)
            : Tree_(tree)
        { }

        TokenIndex operator[](TokenIndex index) const
        {
            return Tree_.MatchingDelimiter(index);
        }

    private:
        const parser::SyntaxTree& Tree_;
    };

    // Keep one reusable allocation per element type and thread. Each active
    // formatter owns its buffers; only completed runs return them here.
    template <class T>
    static std::vector<T>& ScratchBuffer()
    {
        thread_local std::vector<T> buffer;
        return buffer;
    }

    template <class T>
    static std::vector<T> AcquireBuffer(size_t size, const T& value)
    {
        auto buffer = std::move(ScratchBuffer<T>());
        buffer.assign(size, value);
        return buffer;
    }

    template <class T>
    static std::vector<T> AcquireEmptyBuffer()
    {
        auto buffer = std::move(ScratchBuffer<T>());
        buffer.clear();
        return buffer;
    }

    template <class T>
    static void RecycleBuffer(std::vector<T>& buffer)
    {
        buffer.clear();
        auto& scratch = ScratchBuffer<T>();
        if (buffer.capacity() > scratch.capacity()) {
            scratch = std::move(buffer);
        }
    }

public:
    Formatter(const parser::SyntaxTree& tree, const Style& style)
        : Tree_(tree)
        , Style_(style)
        , Pair_(tree)
        , Braces_(AcquireBuffer(tree.TokenCount(), BraceKind::None))
        , TokenFlags_(AcquireBuffer(tree.TokenCount(), TokenFlags{}))
        , Lambdas_(AcquireBuffer(tree.TokenCount(), LambdaInfo{}))
        , OutputPositions_(AcquireBuffer(tree.TokenCount(), OutputPosition{}))
        , MemberChainIndentShift_(
            AcquireBuffer<int32_t>(
                tree.TokenCount(),
                std::numeric_limits<int32_t>::min()))
        , PrefixWidth_(
            AcquireBuffer<size_t>(
                static_cast<size_t>(tree.TokenCount()) + 1,
                0))
        , Layouts_(AcquireEmptyBuffer<Layout>())
        , NamespaceComments_(AcquireEmptyBuffer<NamespaceComment>())
        , PreservedRanges_(AcquireEmptyBuffer<parser::TokenRange>())
        , BraceInsertions_(AcquireEmptyBuffer<BraceInsertion>())
    {
        auto sourceSize = tree.Source().size();
        auto extra =
            std::max(sourceSize / 8, static_cast<size_t>(tree.TokenCount()));
        Output_.reserve(
            sourceSize <= std::numeric_limits<size_t>::max() - extra
                ? sourceSize + extra
                : sourceSize);
        ClassifyAngles();
        AnalyzeSyntax();
        BuildPreservedRanges();
        if (Style_.InsertBraces) {
            ClassifyUnbracedIfBodies();
        }
        ClassifyDeclaratorOperators();
        BuildSpacingAndWidthPrefix();
        Layouts_.reserve(64);
    }

    ~Formatter()
    {
        RecycleBuffer(Braces_);
        RecycleBuffer(TokenFlags_);
        RecycleBuffer(Lambdas_);
        RecycleBuffer(OutputPositions_);
        RecycleBuffer(MemberChainIndentShift_);
        RecycleBuffer(PrefixWidth_);
        RecycleBuffer(Layouts_);
        RecycleBuffer(NamespaceComments_);
        RecycleBuffer(PreservedRanges_);
        RecycleBuffer(BraceInsertions_);
    }

    std::string Run()
    {
        const auto count = Tree_.TokenCount();
        for (TokenIndex index = 0; index < count; ++index) {
            CurrentToken_ = NoToken;
            EmitBraceInsertions(index);
            if (PreservedRangeIndex_ < PreservedRanges_.size() &&
                PreservedRanges_[PreservedRangeIndex_].Begin == index)
            {
                const auto range = PreservedRanges_[PreservedRangeIndex_++];
                if (AtLineStart_ && range.Begin != 0
                    && Tree_.Kind(range.Begin - 1)
                        == TK::PreprocessingDirective)
                {
                    NextLineAbsoluteIndent_ =
                        Tree_.TokenPosition(range.Begin).Column - 1;
                }
                AppendPreservedRange(range);
                for (auto preserved = range.Begin; preserved < range.End;
                    ++preserved)
                {
                    UpdateTemplateDepth(preserved);
                }
                index = range.End - 1;
            } else if (!IsRedundantSemicolon(index)) {
                FormatToken(index);
                UpdateTemplateDepth(index);
            }
        }
        CurrentToken_ = NoToken;
        EmitBraceInsertions(count);
        if (!PreservedThroughEnd_) {
            TrimTrailingSpace();
        }
        if (!PreservedThroughEnd_ && count != 0) {
            size_t trailingNewlines = 0;
            for (char character : Tree_.WhitespaceAfter(count - 1)) {
                trailingNewlines += character == '\n';
            }
            trailingNewlines = std::max(trailingNewlines, size_t{1});

            size_t outputNewlines = 0;
            for (auto i = Output_.size();
                i != 0 && Output_[i - 1] == '\n'; --i)
            {
                ++outputNewlines;
            }
            Output_.append(trailingNewlines - std::min(
                trailingNewlines,
                outputNewlines), '\n');
        }
        return std::move(Output_);
    }

private:
    TokenIndex FindOpeningParenthesis(TokenIndex closing) const
    {
        size_t depth = 1;
        for (auto index = closing; index != 0;) {
            --index;
            auto kind = Tree_.Kind(index);
            if (kind == TK::Operator_CloseParenthesis) {
                ++depth;
            } else if (kind == TK::Operator_OpenParenthesis && --depth == 0) {
                return index;
            }
        }
        return NoToken;
    }

    bool IsRedundantSemicolon(TokenIndex index) const
    {
        return index != 0 &&
            Tree_.Kind(index) == TK::Operator_Semicolon &&
            Tree_.Kind(index - 1) == TK::Operator_Semicolon &&
            Tree_.Role(index) == PR::Generic &&
            Tree_.Role(index - 1) == PR::Generic &&
            !Tree_.WhitespaceBefore(index).contains('\n');
    }

    // Syntax analysis -------------------------------------------------------

    void ClassifyAngles()
    {
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            TokenFlags_[index].AngleDelimiter = Tree_.Role(index) == PR::TemplateDelimiter;
        }
    }

    void UpdateTemplateDepth(TokenIndex index)
    {
        auto kind = Tree_.Kind(index);
        if (kind == TK::Operator_Less && TokenFlags_[index].AngleDelimiter) {
            ++CurrentTemplateDepth_;
        } else if (kind == TK::Operator_Greater && TokenFlags_[index].AngleDelimiter) {
            CurrentTemplateDepth_ -= std::min(CurrentTemplateDepth_, size_t{1});
        } else if (kind == TK::Operator_RightShift && TokenFlags_[index].AngleDelimiter) {
            CurrentTemplateDepth_ -= std::min(CurrentTemplateDepth_, size_t{2});
        }
    }

    bool IsBraceableIfBody(const parser::Node& node) const
    {
        switch (node.Kind) {
        case NK::DeclarationStatement:
        case NK::ExpressionStatement:
        case NK::IfStatement:
        case NK::SwitchStatement:
        case NK::WhileStatement:
        case NK::DoWhileStatement:
        case NK::ForStatement:
        case NK::TryStatement:
        case NK::ReturnStatement:
        case NK::BreakStatement:
        case NK::ContinueStatement:
        case NK::GotoStatement:
        case NK::ThrowStatement:
        case NK::CoYieldStatement:
        case NK::LabelStatement:
            return true;
        default:
            return false;
        }
    }

    bool
    CanInsertBeforeBody(TokenIndex insertion, const parser::Node& body) const
    {
        if (insertion > body.Tokens.Begin) {
            return false;
        }
        for (auto index = insertion; index < body.Tokens.Begin; ++index) {
            if (Tree_.Kind(index) != TK::Comment) {
                return false;
            }
        }
        return true;
    }

    bool StartsPreservedRange(TokenIndex index) const
    {
        return std::ranges::any_of(PreservedRanges_, [index] (const auto& range) {
            return range.Begin == index;
        });
    }

    bool OverlapsPreservedRange(TokenIndex begin, TokenIndex end) const
    {
        for (const auto& range : PreservedRanges_) {
            if (range.End <= begin) {
                continue;
            }
            if (range.Begin >= end) {
                break;
            }
            return true;
        }
        return false;
    }

    void AddIfBodyBraces(
        TokenIndex opening,
        TokenIndex closing,
        const parser::Node& body)
    {
        if (!IsBraceableIfBody(body) || !CanInsertBeforeBody(opening, body) ||
            closing < body.Tokens.End || closing > Tree_.TokenCount() ||
            OverlapsPreservedRange(opening, closing))
        {
            return;
        }
        BraceInsertions_.push_back(
            {.Index = opening, .Kind = BraceInsertionKind::Open});
        BraceInsertions_.push_back(
            {.Index = closing, .Kind = BraceInsertionKind::Close});
    }

    void ClassifyUnbracedIfBodies()
    {
        std::vector<const parser::Node*> nodes;
        nodes.reserve(128);
        nodes.push_back(&Tree_.Root());
        while (!nodes.empty()) {
            auto* node = nodes.back();
            nodes.pop_back();
            for (auto* child = node->FirstChild; child;
                child = child->NextSibling)
            {
                nodes.push_back(child);
            }
            if (node->Kind != NK::IfStatement) {
                continue;
            }

            const parser::Node* condition = nullptr;
            const parser::Node* thenBody = nullptr;
            const parser::Node* elseBody = nullptr;
            for (auto* child = node->FirstChild; child;
                child = child->NextSibling)
            {
                if (child->Kind == NK::Condition) {
                    condition = child;
                    continue;
                }
                if (child->Kind == NK::Comment ||
                    child->Kind == NK::PreprocessingDirective)
                {
                    continue;
                }
                if (!thenBody) {
                    thenBody = child;
                } else if (!elseBody) {
                    elseBody = child;
                }
            }
            if (!thenBody) {
                continue;
            }

            auto headerEnd = condition ? condition->Tokens.End : NoToken;
            if (!condition) {
                for (auto index = node->Tokens.Begin;
                    index < thenBody->Tokens.Begin &&
                    index < Tree_.TokenCount();
                    ++index)
                {
                    if (Tree_.Kind(index) == TK::Keyword_consteval) {
                        headerEnd = index + 1;
                    }
                }
            }
            if (headerEnd == NoToken) {
                continue;
            }

            TokenIndex elseIndex = NoToken;
            for (auto index = thenBody->Tokens.End;
                index < node->Tokens.End && index < Tree_.TokenCount();
                ++index)
            {
                if (Tree_.Kind(index) == TK::Keyword_else) {
                    elseIndex = index;
                    break;
                }
            }
            if (elseIndex != NoToken && !elseBody) {
                continue;
            }

            auto thenEnd = elseIndex == NoToken ? node->Tokens.End : elseIndex;
            AddIfBodyBraces(headerEnd, thenEnd, *thenBody);
            if (elseBody && elseBody->Kind != NK::IfStatement) {
                AddIfBodyBraces(elseIndex + 1, node->Tokens.End, *elseBody);
            }
        }

        std::ranges::sort(
            BraceInsertions_,
            [] (const auto& lhs, const auto& rhs) {
                if (lhs.Index != rhs.Index) {
                    return lhs.Index < rhs.Index;
                }
                return lhs.Kind < rhs.Kind;
            });
    }

    void EmitBraceInsertions(TokenIndex index)
    {
        while (BraceInsertionIndex_ < BraceInsertions_.size() &&
            BraceInsertions_[BraceInsertionIndex_].Index < index)
        {
            ++BraceInsertionIndex_;
        }
        auto begin = BraceInsertionIndex_;
        while (BraceInsertionIndex_ < BraceInsertions_.size() &&
            BraceInsertions_[BraceInsertionIndex_].Index == index)
        {
            ++BraceInsertionIndex_;
        }
        auto end = BraceInsertionIndex_;
        auto cursor = begin;
        while (cursor < end &&
            BraceInsertions_[cursor].Kind == BraceInsertionKind::Close)
        {
            if (Indent_ != 0) {
                --Indent_;
            }
            if (!AtLineStart_) {
                Newline();
            }
            Write("}");
            ++cursor;
            if (cursor < end &&
                BraceInsertions_[cursor].Kind == BraceInsertionKind::Close)
            {
                Newline();
            }
        }
        if (cursor != begin) {
            if (index < Tree_.TokenCount() &&
                Tree_.Kind(index) == TK::Keyword_else)
            {
                Space();
            } else {
                Newline();
            }
        }
        while (cursor < end) {
            bool multilineHeader = index != 0 &&
                Tree_.Kind(index - 1)
                    == TK::Operator_CloseParenthesis &&
                LastControlKeyword_ != NoToken &&
                OutputPositions_[LastControlKeyword_].Line != NoToken &&
                OutputPositions_[LastControlKeyword_].Line != OutputLine_;
            bool ownLine = Style_.ControlBracePlacement
                    == BracePlacement::NewLine || (Style_.ControlBracePlacement
                        == BracePlacement::AttachUnlessMultiline &&
                multilineHeader);
            if (ownLine) {
                Newline();
            } else if (!AtLineStart_) {
                Space();
            }
            Write("{");
            ++Indent_;
            ++cursor;
            bool trailingComment = index < Tree_.TokenCount() &&
                Tree_.Kind(index) == TK::Comment &&
                !Tree_.WhitespaceBefore(index).contains('\n');
            if (trailingComment) {
                Space();
            } else {
                Newline();
            }
        }
    }

    void SetFirstBrace(const parser::Node& node, BraceKind kind)
    {
        auto end = std::min(node.Tokens.End, Tree_.TokenCount());
        for (auto i = node.Tokens.Begin; i < end; ++i) {
            if (Tree_.Kind(i) == TK::Operator_OpenBrace) {
                if (Braces_[i] == BraceKind::Lambda &&
                    kind == BraceKind::Initializer)
                {
                    return;
                }
                Braces_[i] = kind;
                if (Pair_[i] != NoToken) {
                    Braces_[Pair_[i]] = kind;
                }
                return;
            }
        }
    }

    void ClassifyInlineRecord(const parser::Node& node)
    {
        for (auto opening = node.Tokens.Begin;
            opening < node.Tokens.End && opening < Tree_.TokenCount();
            ++opening)
        {
            if (Tree_.Kind(opening) != TK::Operator_OpenBrace) {
                continue;
            }
            auto previous = opening;
            while (previous != node.Tokens.Begin &&
                Tree_.Kind(previous - 1) == TK::Comment)
            {
                --previous;
            }
            if (previous == node.Tokens.Begin) {
                return;
            }
            auto keyword = Tree_.Kind(previous - 1);
            if (keyword != TK::Keyword_class &&
                keyword != TK::Keyword_struct &&
                keyword != TK::Keyword_union)
            {
                return;
            }
            TokenFlags_[opening].InlineRecord = true;
            if (Pair_[opening] != NoToken) {
                TokenFlags_[Pair_[opening]].InlineRecord = true;
            }
            return;
        }
    }

    void ClassifyTemplateParameterPack(const parser::Node& node)
    {
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount(); ++index)
        {
            if (Tree_.Kind(index) == TK::Operator_Ellipsis) {
                TokenFlags_[index].TemplateParameterPack = true;
                return;
            }
        }
    }

    void PreserveFunctionDeclaratorBreak(const parser::Node& node)
    {
        for (auto* child = node.FirstChild; child; child = child->NextSibling) {
            if (child->Kind == NK::InitDeclaratorList &&
                child->Tokens.Begin < Tree_.TokenCount() &&
                Tree_.WhitespaceBefore(child->Tokens.Begin).contains('\n'))
            {
                TokenFlags_[child->Tokens.Begin].PreserveBreakBefore = true;
                return;
            }
        }
    }

    void PreserveUnbracedControlBodyIndent(const parser::Node& node)
    {
        TokenIndex closing = NoToken;
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount();
            ++index)
        {
            if (Tree_.Kind(index) == TK::Operator_OpenParenthesis &&
                Pair_[index] != NoToken)
            {
                closing = Pair_[index];
                break;
            }
        }
        if (closing == NoToken || closing + 1 >= node.Tokens.End) {
            return;
        }

        auto firstBodyToken = closing + 1;
        while (firstBodyToken < node.Tokens.End &&
            Tree_.Kind(firstBodyToken) == TK::Comment)
        {
            ++firstBodyToken;
        }
        if (firstBodyToken >= node.Tokens.End ||
            Tree_.Kind(firstBodyToken) == TK::Operator_OpenBrace)
        {
            return;
        }

        if (Tree_.WhitespaceBefore(closing + 1).contains('\n')) {
            TokenFlags_[closing + 1].PreserveBreakBefore = true;
        }
        for (auto index = closing + 1;
            index < node.Tokens.End && index < Tree_.TokenCount();
            ++index)
        {
            TokenFlags_[index].PreserveLineIndent = true;
        }
    }

    void ClassifyControlBodyBraces(const parser::Node& node)
    {
        for (auto* child = node.FirstChild; child; child = child->NextSibling) {
            if (child->Kind == NK::CompoundStatement) {
                SetFirstBrace(*child, BraceKind::Block);
            }
        }
    }

    void ClassifyDoWhileContinuation(const parser::Node& node)
    {
        TokenIndex continuation = NoToken;
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount();
            ++index)
        {
            if (Tree_.Kind(index) == TK::Keyword_while) {
                continuation = index;
            }
        }
        if (continuation != NoToken) {
            TokenFlags_[continuation].DoWhileContinuation = true;
        }
    }

    bool ContainsFormattingSensitiveMacro(const parser::Node& node) const
    {
        auto end = std::min(node.Tokens.End, Tree_.TokenCount());
        for (auto index = node.Tokens.Begin; index < end; ++index) {
            if (Tree_.Kind(index) == TK::PreprocessingDirective) {
                return true;
            }
            if (Tree_.Kind(index) != TK::Identifier ||
                !IsUppercaseMacroName(Tree_.TokenText(index)) ||
                index + 1 >= end ||
                Tree_.Kind(index + 1) != TK::Operator_OpenParenthesis ||
                Pair_[index + 1] == NoToken)
            {
                continue;
            }
            auto after = Pair_[index + 1] + 1;
            if (after >= end ||
                Tree_.Kind(after) != TK::Operator_Semicolon)
            {
                return true;
            }
        }
        return false;
    }

    void ClassifySwitchCaseIndentation(const parser::Node& node)
    {
        TokenIndex opening = NoToken;
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount();
            ++index)
        {
            if (Tree_.Kind(index) == TK::Operator_OpenBrace &&
                Tree_.Role(index) == PR::BlockDelimiter &&
                Pair_[index] != NoToken)
            {
                opening = index;
                break;
            }
        }
        if (opening == NoToken) {
            return;
        }

        auto closing = Pair_[opening];
        auto switchColumn = Tree_.TokenPosition(node.Tokens.Begin).Column;
        size_t indentedCount = 0;
        size_t outdentedCount = 0;
        bool firstIndented = false;
        bool foundFirst = false;
        for (auto index = opening + 1; index < closing; ++index) {
            if (Tree_.Kind(index) == TK::Operator_OpenBrace &&
                Pair_[index] != NoToken)
            {
                index = Pair_[index];
                continue;
            }
            auto kind = Tree_.Kind(index);
            if (kind != TK::Keyword_case && kind != TK::Keyword_default) {
                continue;
            }
            bool indented = Tree_.TokenPosition(index).Column > switchColumn;
            if (!foundFirst) {
                firstIndented = indented;
                foundFirst = true;
            }
            indentedCount += indented;
            outdentedCount += !indented;
        }
        if (!foundFirst) {
            return;
        }
        bool indentCases = false;
        switch (Style_.IndentCaseLabels) {
        case CaseLabelIndentation::Preserve:
            indentCases = indentedCount == outdentedCount
                ? firstIndented
                : indentedCount > outdentedCount;
            break;
        case CaseLabelIndentation::Outdent:
            indentCases = false;
            break;
        case CaseLabelIndentation::Indent:
            indentCases = true;
            break;
        }

        bool sawLabel = false;
        for (auto index = opening + 1; index < closing; ++index) {
            if (Tree_.Kind(index) == TK::Operator_OpenBrace &&
                Pair_[index] != NoToken)
            {
                index = Pair_[index];
                continue;
            }
            auto kind = Tree_.Kind(index);
            if (kind != TK::Keyword_case && kind != TK::Keyword_default) {
                continue;
            }
            if (indentCases) {
                TokenFlags_[index].IndentedCaseLabel = true;
                if (sawLabel) {
                    TokenFlags_[index].EndIndentedCaseBody = true;
                }
            }
            for (auto colon = index + 1; colon < closing; ++colon) {
                if (Tree_.Kind(colon) != TK::Operator_Colon ||
                    Tree_.Role(colon) != PR::LabelDelimiter)
                {
                    continue;
                }
                TokenFlags_[colon].CaseLabelDelimiter = true;
                if (indentCases) {
                    TokenFlags_[colon].BeginIndentedCaseBody = true;
                }
                auto body = colon + 1;
                while (body < closing && Tree_.Kind(body) == TK::Comment) {
                    ++body;
                }
                if (body < closing && Tree_.Kind(body) == TK::Operator_OpenBrace &&
                    Tree_.Role(body) == PR::BlockDelimiter &&
                    Pair_[body] != NoToken &&
                    !Tree_.WhitespaceBefore(body).contains('\n'))
                {
                    Braces_[body] = BraceKind::CaseBlock;
                    Braces_[Pair_[body]] = BraceKind::CaseBlock;
                }
                break;
            }
            sawLabel = true;
        }
        if (indentCases) {
            TokenFlags_[closing].EndIndentedCaseBody = true;
        }
    }

    void ClassifyBraceTokens()
    {
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            if (Tree_.Kind(index) != TK::Operator_OpenBrace) {
                continue;
            }
            switch (Tree_.Role(index)) {
            case PR::BlockDelimiter:
                Braces_[index] = BraceKind::Block;
                break;
            case PR::RecordDelimiter:
                Braces_[index] = BraceKind::Record;
                break;
            case PR::InitializerDelimiter:
                Braces_[index] = BraceKind::Initializer;
                break;
            case PR::LambdaDelimiter:
                Braces_[index] = BraceKind::Lambda;
                break;
            default:
                break;
            }
            if (Braces_[index] == BraceKind::None && Pair_[index] != NoToken) {
                auto first = index + 1;
                while (first < Pair_[index] &&
                    Tree_.Kind(first) == TK::Comment)
                {
                    ++first;
                }
                if (first < Pair_[index] && IsDesignatedInitializer(first)) {
                    Braces_[index] = BraceKind::Initializer;
                }
            }
            if (Pair_[index] != NoToken) {
                Braces_[Pair_[index]] = Braces_[index];
            }
        }
    }

    void ClassifyLambdaTokens()
    {
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            if (Tree_.Kind(index) != TK::Operator_OpenBracket ||
                Pair_[index] == NoToken ||
                Tree_.Role(index) != PR::LambdaDelimiter)
            {
                continue;
            }
            auto closing = Pair_[index];
            TokenFlags_[index].LambdaDelimiter = true;
            TokenFlags_[closing].LambdaDelimiter = true;
            for (auto after = closing + 1; after < Tree_.TokenCount(); ++after) {
                auto kind = Tree_.Kind(after);
                if (kind == TK::Operator_OpenBrace) {
                    Braces_[after] = BraceKind::Lambda;
                    Lambdas_[after].Capture = index;
                    if (Pair_[after] != NoToken) {
                        Braces_[Pair_[after]] = BraceKind::Lambda;
                    }
                    break;
                }
                if (kind == TK::Operator_Semicolon) {
                    break;
                }
            }
        }
    }

    void ClassifyTopLevelSeparators(const parser::Node& node)
    {
        bool preserveTrailingSeparators = false;
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount(); ++index)
        {
            preserveTrailingSeparators = preserveTrailingSeparators || (Tree_.Kind(index) == TK::Operator_Comma &&
                !Tree_.WhitespaceBefore(index).contains('\n') &&
                Tree_.WhitespaceAfter(index).contains('\n'));
        }

        int depth = 0;
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount(); ++index)
        {
            auto kind = Tree_.Kind(index);
            if (kind == TK::Operator_Less && TokenFlags_[index].AngleDelimiter) {
                ++depth;
            } else if (kind == TK::Operator_Greater &&
                TokenFlags_[index].AngleDelimiter)
            {
                --depth;
            } else if (kind == TK::Operator_RightShift &&
                TokenFlags_[index].AngleDelimiter)
            {
                depth -= std::min(depth, 2);
            } else if (IsOpening(kind)) {
                ++depth;
            } else if (IsClosing(kind)) {
                --depth;
            } else if (kind == TK::Operator_Comma && depth == 0 &&
                !preserveTrailingSeparators)
            {
                if (node.Kind == NK::BaseClause) {
                    TokenFlags_[index].BaseSeparator = true;
                } else {
                    TokenFlags_[index].ConstructorSeparator = true;
                }
            }
        }
    }

    void ClassifyBracketPair(const parser::Node& node)
    {
        auto mark = [&] (TokenIndex index) {
            if (node.Kind == NK::LambdaCaptureList) {
                TokenFlags_[index].LambdaDelimiter = true;
            } else {
                TokenFlags_[index].StructuredBinding = true;
            }
        };
        for (auto index = node.Tokens.Begin;
            index < node.Tokens.End && index < Tree_.TokenCount(); ++index)
        {
            if (Tree_.Kind(index) != TK::Operator_OpenBracket) {
                continue;
            }
            mark(index);
            if (Pair_[index] != NoToken) {
                mark(Pair_[index]);
            }
            return;
        }
    }

    void AnalyzeFunctionDefinition(const parser::Node& node)
    {
        PreserveFunctionDeclaratorBreak(node);
        bool startsAtAttachedTemplateCloser = node.Tokens.Begin != 0 &&
            node.Tokens.Begin < Tree_.TokenCount() && (Tree_.Kind(node.Tokens.Begin) == TK::Operator_Greater ||
            Tree_.Kind(node.Tokens.Begin)
                    == TK::Operator_RightShift) &&
            !Tree_.WhitespaceBefore(node.Tokens.Begin).contains('\n');
        if (!startsAtAttachedTemplateCloser &&
            node.Tokens.Begin < Tree_.TokenCount() && (node.Tokens.Begin == 0 ||
                !TokenFlags_[node.Tokens.Begin - 1].BreakAfter))
        {
            TokenFlags_[node.Tokens.Begin].DefinitionStart = true;
        }

        const parser::Node* body = nullptr;
        for (auto* child = node.FirstChild; child; child = child->NextSibling) {
            if (child->Kind == NK::CompoundStatement ||
                child->Kind == NK::FunctionTryBlock)
            {
                body = child;
            }
        }
        if (!body) {
            return;
        }

        bool isConstructor = false;
        for (auto index = node.Tokens.Begin;
            index < body->Tokens.Begin && index < Tree_.TokenCount(); ++index)
        {
            isConstructor = isConstructor ||
                Tree_.Role(index) == PR::ConstructorInitializerDelimiter;
        }
        SetFirstBrace(
            *body,
            isConstructor ? BraceKind::ConstructorFunction
                          : BraceKind::Function);
    }

    void AnalyzeTemplateDeclaration(const parser::Node& node)
    {
        if (node.Tokens.Begin + 1 >= node.Tokens.End ||
            Tree_.Kind(node.Tokens.Begin) != TK::Keyword_template ||
            Tree_.Kind(node.Tokens.Begin + 1) != TK::Operator_Less)
        {
            return;
        }
        TokenFlags_[node.Tokens.Begin].DefinitionStart = true;
        for (auto index = node.Tokens.Begin + 2; index < node.Tokens.End;
            ++index)
        {
            if ((Tree_.Kind(index) == TK::Operator_Greater ||
                    Tree_.Kind(index) == TK::Operator_RightShift) &&
                Tree_.Role(index) == PR::TemplateDelimiter)
            {
                TokenFlags_[index].BreakAfter = true;
                return;
            }
        }
    }

    void AnalyzeNamespaceDeclaration(const parser::Node& node)
    {
        SetFirstBrace(node, BraceKind::Namespace);
        for (auto opening = node.Tokens.Begin;
            opening < node.Tokens.End && opening < Tree_.TokenCount();
            ++opening)
        {
            if (Tree_.Kind(opening) != TK::Operator_OpenBrace) {
                continue;
            }
            if (Pair_[opening] == NoToken) {
                return;
            }

            std::string text = "// namespace";
            bool hasName = false;
            for (auto index = node.Tokens.Begin; index < opening; ++index) {
                if (Tree_.Kind(index) != TK::Identifier &&
                    Tree_.Kind(index) != TK::Operator_ScopeResolution)
                {
                    continue;
                }
                if (!hasName) {
                    text.push_back(' ');
                    hasName = true;
                }
                text += Tree_.TokenText(index);
            }
            NamespaceComments_.push_back(
                {.Closing = Pair_[opening], .Text = std::move(text)});
            return;
        }
    }

    void AnalyzeSyntax()
    {
        ClassifyBraceTokens();
        ClassifyLambdaTokens();

        struct Entry
        {
            const parser::Node* Node;
            const parser::Node* Parent;
        };

        std::vector<Entry> nodes;
        nodes.reserve(128);
        nodes.push_back({.Node = &Tree_.Root(), .Parent = nullptr});
        while (!nodes.empty()) {
            auto [node, parent] = nodes.back();
            nodes.pop_back();
            switch (node->Kind) {
            case NK::PreprocessingConditional:
                for (auto i = node->Tokens.Begin;
                    i < node->Tokens.End && i < Tree_.TokenCount(); ++i)
                {
                    TokenFlags_[i].PreserveLineIndent = true;
                }
                break;
            case NK::SwitchStatement:
                ClassifyControlBodyBraces(*node);
                if (ContainsFormattingSensitiveMacro(*node)) {
                    AddPreservedRange(node->Tokens);
                } else {
                    ClassifySwitchCaseIndentation(*node);
                    PreserveUnbracedControlBodyIndent(*node);
                }
                break;
            case NK::IfStatement:
                ClassifyControlBodyBraces(*node);
                break;
            case NK::WhileStatement:
            case NK::ForStatement:
                ClassifyControlBodyBraces(*node);
                PreserveUnbracedControlBodyIndent(*node);
                break;
            case NK::DoWhileStatement:
                ClassifyControlBodyBraces(*node);
                ClassifyDoWhileContinuation(*node);
                break;
            case NK::FunctionDeclaration:
                PreserveFunctionDeclaratorBreak(*node);
                break;
            case NK::FunctionDefinition:
                AnalyzeFunctionDefinition(*node);
                break;
            case NK::BaseClause:
            case NK::ConstructorInitializerList:
                ClassifyTopLevelSeparators(*node);
                break;
            case NK::BracedInitializer:
            case NK::NewInitializer:
                SetFirstBrace(*node, BraceKind::Initializer);
                break;
            case NK::LambdaBody:
                SetFirstBrace(*node, BraceKind::Lambda);
                break;
            case NK::RequirementBody:
                SetFirstBrace(*node, BraceKind::Requirement);
                break;
            case NK::LambdaCaptureList:
            case NK::StructuredBinding:
                ClassifyBracketPair(*node);
                break;
            case NK::TrailingReturnType:
                if (node->Tokens.Begin < node->Tokens.End &&
                    node->Tokens.Begin < Tree_.TokenCount() &&
                    Tree_.Kind(node->Tokens.Begin)
                        == TK::Operator_IndirectClassMemberAccess)
                {
                    TokenFlags_[node->Tokens.Begin].LambdaReturnArrow = true;
                }
                break;
            case NK::TemplateDeclaration:
                AnalyzeTemplateDeclaration(*node);
                break;
            case NK::NamespaceDeclaration:
                AnalyzeNamespaceDeclaration(*node);
                break;
            case NK::RecordDeclaration:
                SetFirstBrace(*node, BraceKind::Record);
                ClassifyInlineRecord(*node);
                break;
            case NK::TemplateParameter:
                ClassifyTemplateParameterPack(*node);
                break;
            case NK::LinkageSpecification:
                SetFirstBrace(*node, BraceKind::UnindentedBlock);
                break;
            case NK::EnumDeclaration:
                SetFirstBrace(*node, BraceKind::Enum);
                for (auto i = node->Tokens.Begin;
                    i < node->Tokens.End && i < Tree_.TokenCount();
                    ++i)
                {
                    if (Tree_.Kind(i) == TK::Operator_OpenBrace) {
                        break;
                    }
                    if (Tree_.Kind(i) == TK::Operator_Colon) {
                        TokenFlags_[i].SpacedColon = true;
                    }
                }
                break;
            case NK::AmbiguousStatement:
            case NK::Error:
            case NK::UnexpectedToken:
                AddPreservedRange(node->Tokens);
                break;
            case NK::UnparsedSyntax:
                if (parent && parent->Kind == NK::ExpressionStatement) {
                    AddPreservedRange(parent->Tokens);
                } else {
                    AddPreservedRange(node->Tokens);
                }
                break;
            case NK::MissingToken:
                if (parent) {
                    AddPreservedRange(parent->Tokens);
                }
                break;
            default:
                break;
            }
            for (auto* child = node->FirstChild; child;
                child = child->NextSibling)
            {
                nodes.push_back({.Node = child, .Parent = node});
            }
        }
        std::ranges::sort(NamespaceComments_, {}, &NamespaceComment::Closing);
    }

    // Source-preservation analysis -----------------------------------------

    const std::string* NamespaceCommentFor(TokenIndex closing) const
    {
        auto it = std::ranges::lower_bound(
            NamespaceComments_,
            closing,
            {},
            &NamespaceComment::Closing);
        return it != NamespaceComments_.end() && it->Closing == closing
            ? &it->Text
            : nullptr;
    }

    void AddPreservedRange(parser::TokenRange range)
    {
        range.End = std::min(range.End, Tree_.TokenCount());
        if (range.Begin < range.End) {
            PreservedRanges_.push_back(range);
        }
    }

    bool UsesPortabilityDialect() const
    {
        // Portability macros in third-party headers often define a distinct
        // dialect. Reformatting those files can misinterpret macro-controlled
        // declarations and create a large unrelated rewrite.
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            if (Tree_.TokenText(index).ends_with("_NOEXCEPT")) {
                return true;
            }
        }
        return false;
    }

    bool IsLargeInitializer(TokenIndex index) const
    {
        constexpr TokenIndex LargeInitializerTokenCount = 256;
        return Tree_.Kind(index) == TK::Operator_OpenBrace &&
            Braces_[index] == BraceKind::Initializer &&
            Pair_[index] != NoToken &&
            Pair_[index] - index >= LargeInitializerTokenCount;
    }

    void PreserveInitializersWhenFileContainsLargeTable()
    {
        bool containsLargeInitializer = false;
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            containsLargeInitializer =
                containsLargeInitializer || IsLargeInitializer(index);
        }
        if (!containsLargeInitializer) {
            return;
        }

        // Large aggregate tables often use deliberate row packing and column
        // alignment. Preserve every initializer in the same generated file so
        // related tables retain one consistent representation.
        for (TokenIndex index = 0; index < Tree_.TokenCount(); ++index) {
            if (Tree_.Kind(index) == TK::Operator_OpenBrace &&
                Braces_[index] == BraceKind::Initializer &&
                Pair_[index] != NoToken)
            {
                AddPreservedRange(
                    {.Begin = index, .End = Pair_[index] + 1});
                index = Pair_[index];
            }
        }
    }

    parser::TokenRange RequiresClauseRange(TokenIndex requiresIndex) const
    {
        auto begin = requiresIndex;
        while (begin != 0) {
            auto previous = Tree_.Kind(begin - 1);
            if (previous == TK::Operator_Semicolon || ((previous == TK::Operator_OpenBrace ||
                    previous == TK::Operator_CloseBrace) &&
                    IsBlockBrace(Braces_[begin - 1])))
            {
                break;
            }
            --begin;
        }

        auto end = requiresIndex + 1;
        while (end < Tree_.TokenCount()) {
            auto kind = Tree_.Kind(end);
            if (kind == TK::Operator_OpenBrace &&
                IsBlockBrace(Braces_[end]))
            {
                break;
            }
            ++end;
            if (kind == TK::Operator_Semicolon) {
                break;
            }
        }
        return {.Begin = begin, .End = end};
    }

    void PreserveMacroInvocation(TokenIndex nameIndex)
    {
        if (nameIndex + 1 >= Tree_.TokenCount() ||
            Tree_.Kind(nameIndex + 1) != TK::Operator_OpenParenthesis ||
            Pair_[nameIndex + 1] == NoToken)
        {
            return;
        }

        auto name = Tree_.TokenText(nameIndex);
        auto opening = nameIndex + 1;
        auto closing = Pair_[opening];
        bool alignedCall = IsUppercaseMacroName(name) &&
            !Tree_.WhitespaceBefore(opening).empty();
        bool declaration =
            IsUppercaseMacroName(name) && name.contains("_METHOD");
        bool alignedArguments = false;
        if (IsUppercaseMacroName(name)) {
            for (auto index = opening + 1; index < closing; ++index) {
                auto whitespace = Tree_.WhitespaceBefore(index);
                if (!whitespace.contains('\n') && whitespace.contains("  ")) {
                    alignedArguments = true;
                    break;
                }
            }
        }
        bool errorEnum = IsUppercaseMacroName(name) &&
            name.ends_with("DEFINE_ERROR_ENUM");
        if (std::ranges::contains(Style_.WhitespaceSensitiveMacros, name) ||
            errorEnum || alignedCall || declaration || alignedArguments)
        {
            AddPreservedRange({.Begin = nameIndex, .End = closing + 1});
        }

        auto after = closing + 1;
        auto invocationEnd = closing;
        if (after < Tree_.TokenCount() &&
            Tree_.Kind(after) == TK::Operator_Semicolon)
        {
            invocationEnd = after++;
        }
        while (after < Tree_.TokenCount() &&
            Tree_.Kind(after) == TK::Comment)
        {
            ++after;
        }
        bool followedByDeclarationMacro = IsUppercaseMacroName(name) &&
            Tree_.WhitespaceAfter(invocationEnd).contains('\n') &&
            after < Tree_.TokenCount() &&
            Tree_.Kind(after) == TK::Identifier &&
            IsUppercaseMacroName(Tree_.TokenText(after));
        if (followedByDeclarationMacro) {
            AddPreservedRange({.Begin = nameIndex, .End = closing + 1});
        }
    }

    void MergePreservedRanges()
    {
        std::ranges::sort(PreservedRanges_, {}, &parser::TokenRange::Begin);
        size_t mergedSize = 0;
        for (auto range : PreservedRanges_) {
            if (mergedSize != 0 &&
                range.Begin < PreservedRanges_[mergedSize - 1].End)
            {
                PreservedRanges_[mergedSize - 1].End =
                    std::max(PreservedRanges_[mergedSize - 1].End, range.End);
            } else {
                PreservedRanges_[mergedSize++] = range;
            }
        }
        PreservedRanges_.resize(mergedSize);
    }

    void BuildPreservedRanges()
    {
        if (UsesPortabilityDialect()) {
            PreservedRanges_.clear();
            AddPreservedRange({.Begin = 0, .End = Tree_.TokenCount()});
            return;
        }
        PreserveInitializersWhenFileContainsLargeTable();

        TokenIndex macroBlockBegin = NoToken;
        size_t macroBlockDepth = 0;
        for (TokenIndex i = 0; i < Tree_.TokenCount(); ++i) {
            auto role = Tree_.Role(i);
            auto kind = Tree_.Kind(i);
            bool unresolvedBrace =
                kind == TK::Operator_OpenBrace && Braces_[i] == BraceKind::None;
            bool unresolvedBracket = kind == TK::Operator_OpenBracket &&
                !TokenFlags_[i].LambdaDelimiter && !TokenFlags_[i].StructuredBinding;
            if ((role == PR::None || role == PR::Generic) && (unresolvedBrace || unresolvedBracket)) {
                auto closing = Pair_[i];
                AddPreservedRange(
                    {.Begin = i,
                     .End = closing == NoToken ? i + 1 : closing + 1});
            }
            if (kind == TK::Invalid) {
                AddPreservedRange({.Begin = i, .End = i + 1});
            }
            auto text = Tree_.TokenText(i);
            if (Tree_.Kind(i) != TK::Identifier && text != "requires") {
                continue;
            }
            if (text == "requires") {
                AddPreservedRange(RequiresClauseRange(i));
            }

            PreserveMacroInvocation(i);

            if (text.starts_with("BEGIN_")) {
                if (macroBlockDepth++ == 0) {
                    macroBlockBegin = i;
                }
            } else if (text.starts_with("END_") && macroBlockDepth != 0 &&
                --macroBlockDepth == 0)
            {
                auto end = i + 1;
                if (end < Tree_.TokenCount() &&
                    Tree_.Kind(end) == TK::Operator_OpenParenthesis &&
                    Pair_[end] != NoToken)
                {
                    end = Pair_[end] + 1;
                }
                if (end < Tree_.TokenCount() &&
                    Tree_.Kind(end) == TK::Operator_Semicolon)
                {
                    ++end;
                }
                AddPreservedRange({.Begin = macroBlockBegin, .End = end});
                macroBlockBegin = NoToken;
            }
        }
        if (macroBlockBegin != NoToken) {
            AddPreservedRange(
                {.Begin = macroBlockBegin, .End = Tree_.TokenCount()});
        }

        MergePreservedRanges();
    }

    // Flat layout and wrapping ---------------------------------------------

    void BuildSpacingAndWidthPrefix()
    {
        size_t width = 0;
        PrefixWidth_[0] = 0;
        for (TokenIndex i = 0; i < Tree_.TokenCount(); ++i) {
            if (i != 0) {
                TokenFlags_[i].SpaceBefore = ComputeNeedsSpace(i - 1, i);
            }
            auto tokenWidth = Tree_.TokenText(i).size() + TokenFlags_[i].SpaceBefore;
            if (width > std::numeric_limits<size_t>::max() - tokenWidth) {
                width = std::numeric_limits<size_t>::max();
            } else {
                width += tokenWidth;
            }
            PrefixWidth_[static_cast<size_t>(i) + 1] = width;
        }
    }

    size_t FlatWidth(TokenIndex begin, TokenIndex end) const
    {
        if (begin >= end) {
            return 0;
        }
        auto width = PrefixWidth_[end] - PrefixWidth_[begin];
        if (begin != 0 && NeedsSpace(begin - 1, begin) && width != 0) {
            --width;
        }
        return width;
    }

    bool IsMacroCallDelimiter(TokenIndex opening) const
    {
        if (opening == 0 || Tree_.Kind(opening - 1) != TK::Identifier) {
            return false;
        }
        auto name = Tree_.TokenText(opening - 1);
        return !name.empty() &&
            std::ranges::all_of(name, [] (char character) {
                return character == '_' || (character >= '0' && character <= '9') || (character >= 'A' &&
                    character <= 'Z');
            });
    }

    bool ForbidsBreakAfter(TokenIndex opening) const
    {
        if (opening == 0 ||
            Tree_.Kind(opening) != TK::Operator_OpenParenthesis)
        {
            return false;
        }
        auto previousKind = Tree_.Kind(opening - 1);
        bool constexprIf = Tree_.TokenText(opening - 1) == "constexpr" &&
            opening >= 2 &&
            Tree_.Kind(opening - 2) == TK::Keyword_if;
        if (IsControlKeyword(previousKind) || constexprIf) {
            return true;
        }
        if (previousKind != TK::Identifier) {
            return false;
        }
        auto name = Tree_.TokenText(opening - 1);
        if (std::ranges::contains(Style_.ForbidBreakAfter, name)) {
            return true;
        }
        bool loggingMacro = IsUppercaseMacroName(name) &&
            name.contains("LOG_");
        if (!loggingMacro) {
            return false;
        }

        auto firstArgument = opening + 1;
        while (firstArgument < Tree_.TokenCount() &&
            Tree_.Kind(firstArgument) == TK::Comment)
        {
            ++firstArgument;
        }
        return firstArgument < Tree_.TokenCount() &&
            Tree_.Kind(firstArgument) == TK::Literal_String;
    }

    bool FollowsForbiddenBreakOpening(TokenIndex index) const
    {
        return index != 0 &&
            Tree_.Kind(index - 1) == TK::Operator_OpenParenthesis &&
            ForbidsBreakAfter(index - 1);
    }

    bool SupportsMultilineLayout(TokenIndex opening) const
    {
        auto role = Tree_.Role(opening);
        return role == PR::CallDelimiter || role == PR::DeclarationDelimiter ||
            role == PR::InitializerDelimiter || role == PR::LambdaDelimiter;
    }

    bool IsExistingMultilineDelimiter(TokenIndex opening) const
    {
        auto closing = Pair_[opening];
        return closing != NoToken &&
            Tree_.TokenPosition(opening).Line
                != Tree_.TokenPosition(closing).Line;
    }

    bool CanRegroupDelimiter(TokenIndex opening) const
    {
        if (!Style_.Regroup || !SupportsMultilineLayout(opening) ||
            !IsExistingMultilineDelimiter(opening) ||
            IsMacroCallDelimiter(opening))
        {
            return false;
        }
        auto closing = Pair_[opening];
        if (closing == NoToken ||
            Column_ + FlatWidth(opening, closing + 1) > Style_.ColumnLimit)
        {
            return false;
        }

        for (auto index = opening + 1; index < closing; ++index) {
            auto kind = Tree_.Kind(index);
            if (kind == TK::Comment || kind == TK::PreprocessingDirective ||
                TokenFlags_[index].BreakAfter)
            {
                return false;
            }
            auto whitespace = Tree_.WhitespaceBefore(index);
            auto firstNewline = whitespace.find('\n');
            if (firstNewline != std::string_view::npos &&
                whitespace.find('\n', firstNewline + 1)
                    != std::string_view::npos)
            {
                return false;
            }
            if (IsOpening(kind) && Pair_[index] != NoToken &&
                Tree_.TokenPosition(index).Line
                    != Tree_.TokenPosition(Pair_[index]).Line)
            {
                return false;
            }
            if (kind == TK::Operator_OpenBrace &&
                IsBlockBrace(Braces_[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool IsRegrouping() const
    {
        return !Layouts_.empty() && Layouts_.back().Regroup;
    }

    bool ShouldBreakDelimiter(TokenIndex opening) const
    {
        auto closing = Pair_[opening];
        if (closing == NoToken || closing == opening + 1) {
            return false;
        }
        if (Tree_.Role(opening) == PR::LambdaDelimiter &&
            Tree_.TokenPosition(opening).Line
                == Tree_.TokenPosition(closing).Line)
        {
            return false;
        }
        if (opening + 2 == closing &&
            Tree_.Kind(opening + 1) == TK::Literal_String &&
            Tree_.TokenText(opening + 1).contains('\n'))
        {
            return false;
        }
        if (Tree_.WhitespaceAfter(opening).contains('\n')) {
            return true;
        }
        if (Tree_.Role(opening) == PR::ConditionDelimiter &&
            IsExistingMultilineDelimiter(opening))
        {
            return true;
        }

        if (!SupportsMultilineLayout(opening)) {
            return false;
        }
        if (IsMacroCallDelimiter(opening) &&
            Tree_.TokenPosition(opening).Line
                != Tree_.TokenPosition(closing).Line)
        {
            return false;
        }
        int depth = 0;
        for (auto i = opening + 1; i < closing; ++i) {
            auto kind = Tree_.Kind(i);
            if (depth == 0 && IsMemberAccess(kind) &&
                Tree_.WhitespaceBefore(i).contains('\n'))
            {
                return Column_ + FlatWidth(opening, i) > Style_.ColumnLimit;
            }
            if (kind == TK::Operator_OpenBrace &&
                Braces_[i] == BraceKind::Lambda)
            {
                return Column_ + FlatWidth(opening, i + 1) > Style_.ColumnLimit;
            }
            if (depth == 0 && IsOpening(kind) && Pair_[i] != NoToken &&
                Tree_.TokenPosition(i).Line
                    != Tree_.TokenPosition(Pair_[i]).Line)
            {
                auto nestedEnd = ForbidsBreakAfter(i) ? Pair_[i] + 1 : i + 1;
                return Column_ + FlatWidth(opening, nestedEnd)
                    > Style_.ColumnLimit;
            }
            if (IsOpening(kind)) {
                ++depth;
            } else if (IsClosing(kind)) {
                --depth;
            } else if (depth == 0 && kind == TK::Operator_Comma &&
                Tree_.WhitespaceAfter(i).contains('\n'))
            {
                return true;
            }
        }
        return Column_ + FlatWidth(opening, closing + 1) > Style_.ColumnLimit;
    }

    size_t EffectiveIndent() const
    {
        return Indent_ + ContinuationIndent_;
    }

    TokenIndex FindMemberChainRoot(TokenIndex index) const
    {
        auto cursor = index;
        while (cursor != 0) {
            auto previous = cursor - 1;
            auto kind = Tree_.Kind(previous);
            if (IsClosing(kind) && Pair_[previous] != NoToken) {
                cursor = Pair_[previous];
                continue;
            }
            if (IsWord(kind) || IsMemberAccess(kind) ||
                kind == TK::Operator_ScopeResolution)
            {
                cursor = previous;
                continue;
            }
            break;
        }
        return cursor;
    }

    size_t ShiftedMemberIndent(TokenIndex index)
    {
        auto root = FindMemberChainRoot(index);
        auto sourceMemberIndent =
            static_cast<std::ptrdiff_t>(Tree_.TokenPosition(index).Column - 1);
        if (OutputPositions_[root].Column == NoToken) {
            return static_cast<size_t>(sourceMemberIndent);
        }
        auto sourceRootColumn = static_cast<std::ptrdiff_t>(
            Tree_.TokenPosition(root).Column - 1);
        auto outputRootColumn =
            static_cast<std::ptrdiff_t>(OutputPositions_[root].Column);
        auto sourceLineToken = root;
        auto sourceLine = Tree_.TokenPosition(root).Line;
        while (sourceLineToken != 0 &&
            Tree_.TokenPosition(sourceLineToken - 1).Line == sourceLine)
        {
            --sourceLineToken;
        }
        auto sourceLineIndent = static_cast<std::ptrdiff_t>(
            Tree_.TokenPosition(sourceLineToken).Column - 1);
        if (!IsMemberAccess(Tree_.Kind(root)) &&
            sourceRootColumn == sourceLineIndent &&
            sourceMemberIndent < sourceRootColumn)
        {
            return static_cast<size_t>(outputRootColumn + Style_.IndentWidth);
        }

        auto& cachedShift = MemberChainIndentShift_[root];
        if (cachedShift == std::numeric_limits<int32_t>::min()) {
            std::ptrdiff_t shift = 0;
            if (sourceRootColumn == sourceLineIndent) {
                shift = outputRootColumn - sourceRootColumn;
            } else if (outputRootColumn
                == static_cast<std::ptrdiff_t>(CurrentLineIndent_))
            {
                shift = outputRootColumn - sourceMemberIndent;
            }
            cachedShift = static_cast<int32_t>(shift);
        }
        return static_cast<size_t>(
            std::max<std::ptrdiff_t>(0, sourceMemberIndent + cachedShift));
    }

    // Output primitives ----------------------------------------------------

    void StartLine()
    {
        if (!AtLineStart_) {
            return;
        }
        auto computedSpaces = static_cast<std::ptrdiff_t>(
            (EffectiveIndent() + NextLineContinuationIndent_)
            * Style_.IndentWidth);
        auto spaces = NextLineAbsoluteIndent_ != NoToken
            ? NextLineAbsoluteIndent_
            : static_cast<size_t>(std::max<std::ptrdiff_t>(
                0,
                computedSpaces + LayoutIndentOffset_));
        CurrentLineIndent_ = spaces;
        NextLineAbsoluteIndent_ = NoToken;
        NextLineContinuationIndent_ = 0;
        Output_.append(spaces, ' ');
        Column_ = spaces;
        AtLineStart_ = false;
    }

    void TrimTrailingSpace()
    {
        while (!Output_.empty() && (Output_.back() == ' ' || Output_.back() == '\t')) {
            Output_.pop_back();
        }
    }

    void Newline()
    {
        TrimTrailingSpace();
        if (Output_.empty() || Output_.back() != '\n') {
            Output_.push_back('\n');
            ++OutputLine_;
        }
        AtLineStart_ = true;
        Column_ = 0;
    }

    void BlankLine()
    {
        Newline();
        if (Output_.size() < 2 || Output_[Output_.size() - 2] != '\n') {
            Output_.push_back('\n');
            ++OutputLine_;
        }
    }

    void Space()
    {
        if (!AtLineStart_ && !Output_.empty() && Output_.back() != ' ' &&
            Output_.back() != '\t' && Output_.back() != '\n')
        {
            Output_.push_back(' ');
            ++Column_;
        }
    }

    void Write(std::string_view text)
    {
        StartLine();
        if (CurrentToken_ != NoToken) {
            OutputPositions_[CurrentToken_] = {
                .Line = static_cast<TokenIndex>(OutputLine_),
                .Column = static_cast<TokenIndex>(Column_),
            };
            if (IsControlKeyword(Tree_.Kind(CurrentToken_))) {
                LastControlKeyword_ = CurrentToken_;
            }
        }
        Output_.append(text);
        OutputLine_ += std::ranges::count(text, '\n');
        auto lastNewline = text.rfind('\n');
        if (lastNewline == std::string_view::npos) {
            Column_ += text.size();
            return;
        }
        Column_ = text.size() - lastNewline - 1;
        AtLineStart_ = lastNewline + 1 == text.size();
    }

    void AppendRaw(std::string_view text)
    {
        if (text.empty()) {
            return;
        }
        Output_.append(text);
        OutputLine_ += std::ranges::count(text, '\n');
        auto lastNewline = text.rfind('\n');
        if (lastNewline == std::string_view::npos) {
            Column_ += text.size();
            AtLineStart_ = false;
        } else {
            Column_ = text.size() - lastNewline - 1;
            AtLineStart_ = lastNewline + 1 == text.size();
        }
    }

    void AppendPreservedRange(parser::TokenRange range)
    {
        auto leading = Tree_.WhitespaceBefore(range.Begin);
        if (!Output_.empty() && AtLineStart_ && leading.contains('\n')) {
            size_t existingNewlines = 0;
            for (auto i = Output_.size(); i != 0 && Output_[i - 1] == '\n'; --i) {
                ++existingNewlines;
            }
            size_t consumedNewlines = 0;
            size_t consumedCharacters = 0;
            while (consumedCharacters < leading.size() &&
                consumedNewlines < existingNewlines)
            {
                if (leading[consumedCharacters++] == '\n') {
                    ++consumedNewlines;
                }
            }
            leading.remove_prefix(consumedCharacters);
        } else if (!Output_.empty() && Output_.back() == ' ' && !leading.empty() && (leading.front() == ' ' ||
                leading.front() == '\t'))
        {
            leading.remove_prefix(1);
        }
        if (AtLineStart_) {
            auto lastNewline = leading.rfind('\n');
            auto indentationBegin = lastNewline == std::string_view::npos
                ? size_t{0}
                : lastNewline + 1;
            if (std::ranges::all_of(
                leading.substr(indentationBegin),
                [] (char character) {
                    return character == ' ' || character == '\t';
                }))
            {
                leading.remove_suffix(leading.size() - indentationBegin);
            }
        }
        AppendRaw(leading);
        if (AtLineStart_) {
            StartLine();
        }
        auto indentationShift = static_cast<std::ptrdiff_t>(Column_)
            - static_cast<std::ptrdiff_t>(
                Tree_.TokenPosition(range.Begin).Column - 1);
        auto sourceStartLine = Tree_.TokenPosition(range.Begin).Line;
        for (auto index = range.Begin; index < range.End; ++index) {
            OutputPositions_[index].Line = static_cast<TokenIndex>(
                OutputLine_ + Tree_.TokenPosition(index).Line
                    - sourceStartLine);
            if (IsControlKeyword(Tree_.Kind(index))) {
                LastControlKeyword_ = index;
            }
        }
        AppendRaw(Tree_.SourceText(range));
        auto trailing = Tree_.WhitespaceAfter(range.End - 1);
        auto lastNewline = trailing.rfind('\n');
        if (lastNewline != std::string_view::npos &&
            std::ranges::all_of(
                trailing.substr(lastNewline + 1),
                [] (char character) {
                    return character == ' ' || character == '\t';
                }))
        {
            trailing.remove_suffix(trailing.size() - lastNewline - 1);
        }
        AppendRaw(trailing);
        if (range.End < Tree_.TokenCount() && trailing.contains('\n')) {
            auto sourceIndent = static_cast<std::ptrdiff_t>(
                Tree_.TokenPosition(range.End).Column - 1);
            NextLineAbsoluteIndent_ = static_cast<size_t>(
                std::max<std::ptrdiff_t>(0, sourceIndent + indentationShift));
        }
        PreservedThroughEnd_ = range.End == Tree_.TokenCount();
    }

    size_t OriginalNewlineCount(TokenIndex index) const
    {
        if (index == 0) {
            return 0;
        }
        return std::ranges::count(Tree_.WhitespaceBefore(index), '\n');
    }

    bool HasOriginalBlankLine(TokenIndex index) const
    {
        return OriginalNewlineCount(index) >= 2;
    }

    bool IsStructuralBoundary(TK kind) const
    {
        return kind == TK::Operator_Semicolon || kind == TK::Operator_CloseBrace ||
            kind == TK::Comment || kind == TK::PreprocessingDirective;
    }

    void MaybePreserveBlankLine(TokenIndex index)
    {
        bool separatesMemberChain =
            index < Tree_.TokenCount() && IsMemberAccess(Tree_.Kind(index));
        if (!AtLineStart_ || index == 0 || !HasOriginalBlankLine(index) || (!IsStructuralBoundary(
            Tree_.Kind(index - 1)) &&
                !TokenFlags_[index - 1].BreakAfter && !separatesMemberChain))
        {
            return;
        }
        if (Output_.empty()) {
            return;
        }
        size_t trailingNewlines = 0;
        for (auto i = Output_.size(); i != 0 && Output_[i - 1] == '\n'; --i) {
            ++trailingNewlines;
        }
        auto desiredNewlines = OriginalNewlineCount(index);
        if (trailingNewlines < desiredNewlines) {
            auto addedNewlines = desiredNewlines - trailingNewlines;
            Output_.append(addedNewlines, '\n');
            OutputLine_ += addedNewlines;
        }
    }

    // Token role and spacing decisions -------------------------------------

    bool FollowsCast(TokenIndex index) const
    {
        if (index == 0 ||
            Tree_.Kind(index - 1) != TK::Operator_CloseParenthesis ||
            Tree_.Role(index - 1) != PR::CastDelimiter)
        {
            return false;
        }
        auto opening = Pair_[index - 1];
        if (opening != NoToken && opening != 0) {
            auto precedingText = Tree_.TokenText(opening - 1);
            if (precedingText == "sizeof" || precedingText == "alignof" ||
                precedingText == "decltype" || precedingText == "typeid")
            {
                return false;
            }
        }
        return true;
    }

    bool ComputeDeclaratorOperator(TokenIndex index) const
    {
        auto role = Tree_.Role(index);
        return !FollowsCast(index) && (role == PR::PointerDeclarator ||
            role == PR::ReferenceDeclarator);
    }

    void ClassifyDeclaratorOperators()
    {
        for (TokenIndex i = 0; i < Tree_.TokenCount(); ++i) {
            TokenFlags_[i].DeclaratorOperator = ComputeDeclaratorOperator(i);
        }
    }

    bool IsDeclaratorOperator(TokenIndex index) const
    {
        return TokenFlags_[index].DeclaratorOperator;
    }

    bool StartsParenthesizedDeclarator(TokenIndex opening) const
    {
        if (Tree_.Kind(opening) != TK::Operator_OpenParenthesis ||
            Tree_.Role(opening) != PR::DeclarationDelimiter ||
            Pair_[opening] == NoToken ||
            Pair_[opening] + 1 >= Tree_.TokenCount() || (Tree_.Kind(Pair_[opening] + 1)
                    != TK::Operator_OpenParenthesis &&
                Tree_.Kind(Pair_[opening] + 1)
                    != TK::Operator_OpenBracket))
        {
            return false;
        }
        for (auto index = opening + 1; index < Pair_[opening]; ++index) {
            if (IsDeclaratorOperator(index)) {
                return true;
            }
        }
        return false;
    }

    bool FollowsParenthesizedDeclaratorOperator(TokenIndex index) const
    {
        if (index == 0 || !IsDeclaratorOperator(index - 1)) {
            return false;
        }
        for (auto opening = index - 1; opening != 0; --opening) {
            if (Tree_.Kind(opening - 1) != TK::Operator_OpenParenthesis) {
                continue;
            }
            if (Pair_[opening - 1] != NoToken &&
                Pair_[opening - 1] >= index &&
                StartsParenthesizedDeclarator(opening - 1))
            {
                return true;
            }
        }
        return false;
    }

    bool FollowsSeparatedDeclaratorOperator(TokenIndex index) const
    {
        if (index == 0 || !IsDeclaratorOperator(index - 1)) {
            return false;
        }
        auto cursor = index - 1;
        while (cursor != 0 && IsDeclaratorOperator(cursor - 1)) {
            --cursor;
        }
        return cursor != 0 && Tree_.Kind(cursor - 1) == TK::Operator_Comma &&
            Tree_.Role(cursor - 1) == PR::ListSeparator;
    }

    bool CanEndExpression(TokenIndex index) const
    {
        auto kind = Tree_.Kind(index);
        return kind == TK::Identifier || (kind >= TK::Literal_UserDefinedInteger &&
            kind <= TK::Literal_String) ||
            kind == TK::Keyword_false || kind == TK::Keyword_nullptr ||
            kind == TK::Keyword_this || kind == TK::Keyword_true ||
            kind == TK::Operator_CloseParenthesis ||
            kind == TK::Operator_CloseBracket ||
            kind == TK::Operator_CloseBrace || ((kind == TK::Operator_Increment ||
            kind == TK::Operator_Decrement) &&
            index != 0 && CanEndExpression(index - 1));
    }

    bool IsMisclassifiedBinaryOperator(TokenIndex index) const
    {
        if (index == 0 || Tree_.Role(index) != PR::UnaryOperator ||
            Tree_.WhitespaceBefore(index).contains('\n') ||
            !CanEndExpression(index - 1))
        {
            return false;
        }
        auto kind = Tree_.Kind(index);
        return kind == TK::Operator_Star || kind == TK::Operator_Ampersand ||
            kind == TK::Operator_Plus || kind == TK::Operator_Minus;
    }

    bool IsMisclassifiedUnaryOperator(TokenIndex index) const
    {
        if (index == 0 || index + 1 >= Tree_.TokenCount() ||
            !IsBinaryRole(Tree_.Role(index)) ||
            Tree_.Kind(index - 1) != TK::Operator_CloseParenthesis ||
            !Tree_.WhitespaceBefore(index + 1).empty())
        {
            return false;
        }
        auto kind = Tree_.Kind(index);
        return kind == TK::Operator_Star || kind == TK::Operator_Ampersand;
    }

    bool IsUnaryOperator(TokenIndex index) const
    {
        auto kind = Tree_.Kind(index);
        bool castOperand = FollowsCast(index) && (kind == TK::Operator_Star ||
            kind == TK::Operator_Ampersand);
        return castOperand || (Tree_.Role(index) == PR::UnaryOperator &&
            !IsMisclassifiedBinaryOperator(index)) ||
            IsMisclassifiedUnaryOperator(index);
    }

    bool IsAmbiguousCastOperator(TokenIndex index) const
    {
        if (!FollowsCast(index)) {
            return false;
        }
        auto kind = Tree_.Kind(index);
        return kind == TK::Operator_Star || kind == TK::Operator_Ampersand;
    }

    bool IsUncertainContextDependentOperator(TokenIndex index) const
    {
        auto role = Tree_.Role(index);
        return !TokenFlags_[index].LambdaReturnArrow && (role == PR::None || role == PR::Generic) &&
            IsContextDependentOperator(Tree_.Kind(index));
    }

    bool IsKnownBinaryOperator(TokenIndex index) const
    {
        if (IsUnaryOperator(index)) {
            return false;
        }
        return IsBinaryRole(Tree_.Role(index)) ||
            IsMisclassifiedBinaryOperator(index) || (!IsUncertainContextDependentOperator(index) &&
            IsBinaryToken(Tree_.Kind(index)));
    }

    bool IsDesignatedInitializer(TokenIndex index) const
    {
        if (Tree_.Kind(index) != TK::Operator_ClassMemberAccess) {
            return false;
        }
        auto previous = index;
        while (previous != 0) {
            --previous;
            if (Tree_.Kind(previous) != TK::Comment) {
                break;
            }
        }
        auto name = index + 1;
        while (name < Tree_.TokenCount() && Tree_.Kind(name) == TK::Comment) {
            ++name;
        }
        auto initializer = name + 1;
        while (initializer < Tree_.TokenCount() &&
            Tree_.Kind(initializer) == TK::Comment)
        {
            ++initializer;
        }
        return index != 0 && name < Tree_.TokenCount() &&
            initializer < Tree_.TokenCount() && (Tree_.Kind(previous) == TK::Operator_OpenBrace ||
            Tree_.Kind(previous) == TK::Operator_Comma) &&
            Tree_.Kind(name) == TK::Identifier && (Tree_.Kind(initializer) == TK::Operator_Assign ||
            Tree_.Kind(initializer) == TK::Operator_OpenBrace);
    }

    bool IsExponentSign(TokenIndex previous, TokenIndex current) const
    {
        auto currentKind = Tree_.Kind(current);
        if ((currentKind == TK::Operator_Plus ||
                currentKind == TK::Operator_Minus) &&
            Tree_.WhitespaceBefore(current).empty() &&
            Tree_.TokenText(previous).ends_with('e'))
        {
            return true;
        }
        auto previousKind = Tree_.Kind(previous);
        return (previousKind == TK::Operator_Plus ||
            previousKind == TK::Operator_Minus) &&
            previous != 0 && Tree_.WhitespaceBefore(previous).empty() &&
            Tree_.WhitespaceBefore(current).empty() &&
            Tree_.TokenText(previous - 1).ends_with('e');
    }

    bool IsTightMemberAccess(TokenIndex index) const
    {
        auto kind = Tree_.Kind(index);
        return kind == TK::Operator_ScopeResolution ||
            kind == TK::Operator_ClassMemberAccess ||
            kind == TK::Operator_PointerToMemberDereference || (kind == TK::Operator_IndirectClassMemberAccess &&
            !TokenFlags_[index].LambdaReturnArrow) ||
            kind == TK::Operator_IndirectPointerToMemberDereference;
    }

    bool ClosesLambdaTemplateParameterList(TokenIndex index) const
    {
        auto kind = Tree_.Kind(index);
        if (!TokenFlags_[index].AngleDelimiter || (kind != TK::Operator_Greater &&
                kind != TK::Operator_RightShift))
        {
            return false;
        }

        int depth = kind == TK::Operator_RightShift ? 2 : 1;
        for (auto cursor = index; cursor != 0;) {
            --cursor;
            auto current = Tree_.Kind(cursor);
            if (TokenFlags_[cursor].AngleDelimiter &&
                current == TK::Operator_Greater)
            {
                ++depth;
            } else if (TokenFlags_[cursor].AngleDelimiter &&
                current == TK::Operator_RightShift)
            {
                depth += 2;
            } else if (current == TK::Operator_Less && --depth == 0) {
                return cursor != 0 &&
                    Tree_.Kind(cursor - 1)
                        == TK::Operator_CloseBracket &&
                    TokenFlags_[cursor - 1].LambdaDelimiter;
            }
        }
        return false;
    }

    bool SpaceBeforeOpeningParenthesis(
        TokenIndex previous,
        TokenIndex current) const
    {
        auto previousKind = Tree_.Kind(previous);
        auto previousRole = Tree_.Role(previous);
        auto currentRole = Tree_.Role(current);
        if (currentRole == PR::None || currentRole == PR::Generic) {
            return !Tree_.WhitespaceBefore(current).empty();
        }
        if (previous != 0 &&
            Tree_.Kind(previous - 1) == TK::Keyword_operator)
        {
            return false;
        }
        if (previousKind == TK::Keyword_new) {
            return !Tree_.WhitespaceBefore(current).empty();
        }
        if ((IsKnownBinaryOperator(previous) && !TokenFlags_[previous].AngleDelimiter) ||
            ClosesLambdaTemplateParameterList(previous) ||
            StartsParenthesizedDeclarator(current) || (previousKind == TK::Keyword_return &&
                !Tree_.WhitespaceBefore(current).empty()) || (previousKind == TK::Operator_CloseBracket && (previousRole
                == PR::LambdaDelimiter ||
                TokenFlags_[previous].LambdaDelimiter)))
        {
            return true;
        }
        switch (Style_.SpaceBeforeParentheses) {
        case ParenthesisSpacing::Never:
            return false;
        case ParenthesisSpacing::ControlStatements:
            return IsControlKeyword(previousKind) || (Tree_.TokenText(previous) == "constexpr" &&
                previous != 0 &&
                Tree_.TokenText(previous - 1) == "if");
        case ParenthesisSpacing::Always:
            return IsWord(previousKind) ||
                previousKind == TK::Operator_CloseParenthesis ||
                previousKind == TK::Operator_CloseBracket;
        }
        return false;
    }

    bool SpaceBeforeOpeningBracket(
        TokenIndex previous,
        TokenIndex current) const
    {
        auto role = Tree_.Role(current);
        return role == PR::LambdaDelimiter || role == PR::AttributeDelimiter ||
            TokenFlags_[current].LambdaDelimiter || TokenFlags_[current].StructuredBinding ||
            IsDeclaratorOperator(previous);
    }

    bool SpaceBeforeColon(TokenIndex current) const
    {
        auto role = Tree_.Role(current);
        return role == PR::BaseDelimiter || role == PR::BitFieldDelimiter ||
            role == PR::ConditionalDelimiter ||
            role == PR::ConditionDelimiter ||
            role == PR::ConstructorInitializerDelimiter ||
            TokenFlags_[current].SpacedColon ||
            !Tree_.WhitespaceBefore(current).empty();
    }

    DeclaratorAlignment DeclaratorAlignmentFor(TokenIndex index) const
    {
        return Tree_.Kind(index) == TK::Operator_Star
            ? Style_.PointerAlignment
            : Style_.ReferenceAlignment;
    }

    bool ComputeNeedsSpace(TokenIndex previous, TokenIndex current) const
    {
        auto previousKind = Tree_.Kind(previous);
        auto currentKind = Tree_.Kind(current);
        auto previousRole = Tree_.Role(previous);
        auto currentRole = Tree_.Role(current);

        if (IsExponentSign(previous, current)) {
            return false;
        }
        if (IsAmbiguousCastOperator(current) ||
            IsAmbiguousCastOperator(previous))
        {
            return !Tree_.WhitespaceBefore(current).empty();
        }
        if (currentKind == TK::Operator_CloseParenthesis) {
            return Style_.SpaceInsideParentheses &&
                previousKind != TK::Operator_OpenParenthesis;
        }
        if (currentKind == TK::Operator_CloseBracket) {
            bool ordinaryBracket = currentRole != PR::LambdaDelimiter &&
                currentRole != PR::AttributeDelimiter &&
                !TokenFlags_[current].LambdaDelimiter &&
                !TokenFlags_[current].StructuredBinding;
            return Style_.SpaceInsideSquareBrackets && ordinaryBracket &&
                previousKind != TK::Operator_OpenBracket;
        }
        if (currentKind == TK::Operator_Semicolon ||
            currentKind == TK::Operator_Comma)
        {
            return false;
        }
        if (previousKind == TK::Operator_OpenBrace &&
            Braces_[previous] == BraceKind::Initializer)
        {
            switch (Style_.SpaceInsideBracedInitializers) {
            case BracedInitializerSpacing::Preserve:
                return !Tree_.WhitespaceBefore(current).empty();
            case BracedInitializerSpacing::Never:
                return false;
            case BracedInitializerSpacing::Always:
                return true;
            }
        }
        if (currentKind == TK::Operator_ScopeResolution) {
            return previousKind == TK::Keyword_return ||
                !Tree_.WhitespaceBefore(current).empty();
        }
        if (IsUncertainContextDependentOperator(current) ||
            IsUncertainContextDependentOperator(previous))
        {
            return !Tree_.WhitespaceBefore(current).empty();
        }
        if (IsTightMemberAccess(current) ||
            IsTightMemberAccess(previous))
        {
            return false;
        }
        bool currentAngle =
            currentRole == PR::TemplateDelimiter || TokenFlags_[current].AngleDelimiter;
        bool previousAngle =
            previousRole == PR::TemplateDelimiter || TokenFlags_[previous].AngleDelimiter;
        if (currentAngle) {
            bool followsLambdaCapture =
                previousKind == TK::Operator_CloseBracket &&
                TokenFlags_[previous].LambdaDelimiter;
            return previousKind == TK::Keyword_template ||
                followsLambdaCapture;
        }
        if (previousAngle && previousKind == TK::Operator_Less) {
            return false;
        }
        if (Style_.SpaceBeforeTemplateParameterPack &&
            previousKind == TK::Operator_Ellipsis && TokenFlags_[previous].TemplateParameterPack)
        {
            return false;
        }
        if (currentKind == TK::Operator_Ellipsis) {
            if (Style_.SpaceBeforeTemplateParameterPack && TokenFlags_[current].TemplateParameterPack) {
                return true;
            }
            // An ellipsis is otherwise attached in a parameter pack (Args...).
            // Retain the existing choice in a fold expression.
            return currentRole == PR::BinaryOperator &&
                !Tree_.WhitespaceBefore(current).empty();
        }
        if (IsDeclaratorOperator(current)) {
            bool parenthesized =
                previousKind == TK::Operator_OpenParenthesis;
            auto alignment = DeclaratorAlignmentFor(current);
            return !parenthesized && (alignment == DeclaratorAlignment::Right ||
                alignment == DeclaratorAlignment::Middle);
        }
        if (IsDeclaratorOperator(previous) &&
            DeclaratorAlignmentFor(previous)
                != DeclaratorAlignment::Preserve)
        {
            if (FollowsParenthesizedDeclaratorOperator(current) ||
                FollowsSeparatedDeclaratorOperator(current))
            {
                return false;
            }
            auto alignment = DeclaratorAlignmentFor(previous);
            return alignment == DeclaratorAlignment::Left ||
                alignment == DeclaratorAlignment::Middle;
        }
        if (previousKind == TK::Operator_OpenParenthesis) {
            return Style_.SpaceInsideParentheses &&
                currentKind != TK::Operator_CloseParenthesis;
        }
        if (previousKind == TK::Operator_OpenBracket) {
            bool ordinaryBracket = previousRole != PR::LambdaDelimiter &&
                previousRole != PR::AttributeDelimiter &&
                !TokenFlags_[previous].LambdaDelimiter &&
                !TokenFlags_[previous].StructuredBinding;
            return Style_.SpaceInsideSquareBrackets && ordinaryBracket &&
                currentKind != TK::Operator_CloseBracket;
        }
        if (previousKind == TK::Operator_OpenBrace &&
            Braces_[previous] == BraceKind::Lambda)
        {
            return false;
        }
        if (currentKind == TK::Operator_OpenParenthesis) {
            return SpaceBeforeOpeningParenthesis(previous, current);
        }
        if (currentKind == TK::Operator_OpenBracket) {
            return SpaceBeforeOpeningBracket(previous, current);
        }
        if (currentKind == TK::Operator_OpenBrace) {
            if (Braces_[current] == BraceKind::Initializer) {
                return previousKind == TK::Keyword_return ||
                    previousKind == TK::Operator_Assign ||
                    previousKind == TK::Operator_Colon;
            }
            return true;
        }
        if (currentKind == TK::Operator_Colon) {
            return SpaceBeforeColon(current);
        }
        if (previousKind == TK::Operator_Colon) {
            return true;
        }
        if (previousKind == TK::Keyword_operator) {
            return IsWord(currentKind) &&
                !(currentKind >= TK::Literal_UserDefinedInteger &&
                currentKind <= TK::Literal_String);
        }
        if (IsKnownBinaryOperator(current) ||
            IsKnownBinaryOperator(previous) || TokenFlags_[current].LambdaReturnArrow ||
            TokenFlags_[previous].LambdaReturnArrow)
        {
            return true;
        }
        if (IsUnaryOperator(previous) || previousKind == TK::Operator_Complement ||
            previousKind == TK::Operator_Not ||
            previousKind == TK::Operator_Increment ||
            previousKind == TK::Operator_Decrement)
        {
            return false;
        }
        if (currentKind == TK::Operator_Increment ||
            currentKind == TK::Operator_Decrement)
        {
            return previousKind == TK::Keyword_return ||
                previousKind == TK::Keyword_co_return;
        }
        if (IsDeclaratorOperator(previous)) {
            return !IsDeclaratorOperator(current) &&
                !FollowsParenthesizedDeclaratorOperator(current) &&
                !FollowsSeparatedDeclaratorOperator(current);
        }
        if (IsWord(previousKind) && IsWord(currentKind)) {
            if (currentKind == TK::Identifier &&
                previousKind >= TK::Literal_UserDefinedInteger &&
                previousKind <= TK::Literal_String &&
                Tree_.WhitespaceBefore(current).empty())
            {
                return false;
            }
            return true;
        }
        return !Tree_.WhitespaceBefore(current).empty();
    }

    bool NeedsSpace(TokenIndex, TokenIndex current) const
    {
        return TokenFlags_[current].SpaceBefore;
    }

    // Dynamic layout state -------------------------------------------------

    bool IsInsideParentheses() const
    {
        for (auto i = Layouts_.rbegin(); i != Layouts_.rend(); ++i) {
            auto kind = Tree_.Kind(i->Opening);
            if (kind == TK::Operator_OpenParenthesis ||
                kind == TK::Operator_OpenBracket)
            {
                return true;
            }
            if (Braces_[i->Opening] == BraceKind::Lambda &&
                TokenFlags_[i->Opening].CompactLambda)
            {
                return true;
            }
            if (IsBlockBrace(Braces_[i->Opening])) {
                return false;
            }
        }
        return false;
    }

    bool MakeInnermostDelimiterMultiline()
    {
        for (auto i = Layouts_.rbegin(); i != Layouts_.rend(); ++i) {
            auto kind = Tree_.Kind(i->Opening);
            if (kind == TK::Operator_OpenParenthesis ||
                kind == TK::Operator_OpenBracket)
            {
                if (!i->Multiline) {
                    i->Multiline = true;
                    i->BreakElements = true;
                    ++ContinuationIndent_;
                }
                return true;
            }
            if (kind == TK::Operator_OpenBrace) {
                return false;
            }
        }
        return false;
    }

    bool IsLogicalOperator(TokenIndex index) const
    {
        auto kind = Tree_.Kind(index);
        return IsBinaryRole(Tree_.Role(index)) && (kind == TK::Operator_And || kind == TK::Operator_Or);
    }

    bool ShouldBreakAfterLogicalOperator(TokenIndex index) const
    {
        if (IsRegrouping() || !IsLogicalOperator(index)) {
            return false;
        }
        auto operand = index + 1;
        if (operand < Tree_.TokenCount() && IsOpening(Tree_.Kind(operand)) &&
            Pair_[operand] != NoToken &&
            Column_ + 2 + Tree_.TokenText(index).size()
                    + Tree_.TokenText(operand).size()
                <= Style_.ColumnLimit)
        {
            return false;
        }
        if (Tree_.WhitespaceBefore(index).contains('\n') ||
            Tree_.WhitespaceAfter(index).contains('\n'))
        {
            return true;
        }

        int depth = 0;
        auto end = index + 1;
        for (; end < Tree_.TokenCount(); ++end) {
            auto kind = Tree_.Kind(end);
            if (IsOpening(kind)) {
                ++depth;
            } else if (IsClosing(kind)) {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (depth == 0 && (IsLogicalOperator(end) || kind == TK::Operator_Comma ||
                    kind == TK::Operator_Semicolon))
            {
                break;
            }
        }
        if (end < Tree_.TokenCount() && IsClosing(Tree_.Kind(end))) {
            ++end;
            if (end < Tree_.TokenCount() &&
                Tree_.Kind(end) == TK::Operator_OpenBrace)
            {
                ++end;
            }
        }
        return Column_ + 1 + FlatWidth(index, end) > Style_.ColumnLimit;
    }

    bool ShouldBreakBeforeMemberAccess(TokenIndex index) const
    {
        auto kind = Tree_.Kind(index);
        if (!IsMemberAccess(kind)) {
            return false;
        }
        if (Tree_.WhitespaceBefore(index).contains('\n')) {
            return true;
        }
        if (IsUncertainContextDependentOperator(index)) {
            return false;
        }

        int depth = 0;
        auto end = index + 1;
        for (; end < Tree_.TokenCount(); ++end) {
            auto current = Tree_.Kind(end);
            if (current == TK::Operator_OpenBrace &&
                Braces_[end] == BraceKind::Lambda)
            {
                return Column_ + FlatWidth(index, end + 1) > Style_.ColumnLimit;
            }
            if (depth == 0 && IsOpening(current) && Pair_[end] != NoToken &&
                SupportsMultilineLayout(end))
            {
                // Prefer wrapping the nested call or initializer over splitting
                // a member-access expression which still fits through its
                // opening delimiter.
                return Column_ + FlatWidth(index, end + 1) > Style_.ColumnLimit;
            }
            if (IsOpening(current) && Pair_[end] != NoToken &&
                Tree_.TokenPosition(end).Line
                    != Tree_.TokenPosition(Pair_[end]).Line)
            {
                return Column_ + FlatWidth(index, end + 1) > Style_.ColumnLimit;
            }
            if (IsOpening(current)) {
                ++depth;
            } else if (IsClosing(current)) {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (depth == 0 && (IsMemberAccess(current) || IsKnownBinaryOperator(end) ||
                    current == TK::Operator_Semicolon ||
                    current == TK::Operator_Comma))
            {
                break;
            }
        }
        return Column_ + FlatWidth(index, end) > Style_.ColumnLimit;
    }

    void PushLayout(
        TokenIndex index,
        bool multiline,
        bool breakElements,
        bool closingOwnLine,
        bool indents,
        bool regroup = false)
    {
        Layouts_.push_back(
            {.Opening = index,
             .Closing = Pair_[index],
             .Multiline = multiline,
             .BreakElements = breakElements,
             .ClosingOwnLine = closingOwnLine,
             .Indents = indents,
             .Regroup = regroup,
             .IndentOffset = 0,
             .OpeningLineIndent = CurrentLineIndent_});
    }

    void PopLayout()
    {
        LayoutIndentOffset_ -= Layouts_.back().IndentOffset;
        Layouts_.pop_back();
    }

    size_t ShiftedSourceIndent(const Layout& layout, TokenIndex index) const
    {
        auto sourceLineToken = layout.Opening;
        auto sourceLine = Tree_.TokenPosition(sourceLineToken).Line;
        while (sourceLineToken != 0 &&
            Tree_.TokenPosition(sourceLineToken - 1).Line == sourceLine)
        {
            --sourceLineToken;
        }
        auto sourceLineIndent = static_cast<std::ptrdiff_t>(
            Tree_.TokenPosition(sourceLineToken).Column - 1);
        auto shift = static_cast<std::ptrdiff_t>(layout.OpeningLineIndent)
            - sourceLineIndent;
        auto sourceIndent = static_cast<std::ptrdiff_t>(
            Tree_.TokenPosition(index).Column - 1);
        return static_cast<size_t>(
            std::max<std::ptrdiff_t>(0, sourceIndent + shift));
    }

    void DiscardLayoutsClosedInPreservedRanges(TokenIndex index)
    {
        while (!Layouts_.empty() && Layouts_.back().Closing < index) {
            const auto& layout = Layouts_.back();
            auto openingKind = Tree_.Kind(layout.Opening);
            if (layout.Indents && Indent_ != 0) {
                --Indent_;
            }
            if (layout.Multiline && ContinuationIndent_ != 0 && (openingKind == TK::Operator_OpenParenthesis ||
                    openingKind == TK::Operator_OpenBracket ||
                    Braces_[layout.Opening] == BraceKind::Initializer))
            {
                --ContinuationIndent_;
            }
            PopLayout();
        }
    }

    // Token emitters -------------------------------------------------------

    void FormatComment(TokenIndex index)
    {
        bool trailing = index != 0 &&
            !Tree_.WhitespaceBefore(index).contains('\n') && !AtLineStart_;
        if (!trailing) {
            auto next = index + 1;
            while (next < Tree_.TokenCount() && Tree_.Kind(next) == TK::Comment) {
                ++next;
            }
            if (next < Tree_.TokenCount() && (Tree_.Kind(next) == TK::Keyword_case ||
                    Tree_.Kind(next) == TK::Keyword_default))
            {
                NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
            } else if (next < Tree_.TokenCount() &&
                Tree_.Kind(next) == TK::Operator_Colon &&
                Tree_.Role(next) == PR::ConstructorInitializerDelimiter)
            {
                NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
            } else if (next < Tree_.TokenCount() &&
                IsMemberAccess(Tree_.Kind(next)) &&
                !TokenFlags_[next].LambdaReturnArrow &&
                !IsUncertainContextDependentOperator(next) &&
                Tree_.WhitespaceBefore(next).contains('\n'))
            {
                NextLineAbsoluteIndent_ = Tree_.TokenPosition(next).Column - 1;
            }
        }
        if (trailing) {
            if (Tree_.Kind(index - 1) != TK::Operator_OpenParenthesis &&
                Tree_.Kind(index - 1) != TK::Operator_OpenBracket &&
                !(
                    Tree_.Kind(index - 1) == TK::Operator_OpenBrace &&
                    Braces_[index - 1] == BraceKind::Initializer))
            {
                Space();
            }
            auto sourceSpacing = Tree_.WhitespaceBefore(index).size();
            size_t outputSpacing = 0;
            for (auto i = Output_.size();
                i != 0 && (Output_[i - 1] == ' ' || Output_[i - 1] == '\t');
                --i)
            {
                ++outputSpacing;
            }
            if (sourceSpacing > outputSpacing) {
                auto additionalSpacing = sourceSpacing - outputSpacing;
                Output_.append(additionalSpacing, ' ');
                Column_ += additionalSpacing;
            }
        } else if (!AtLineStart_) {
            Newline();
        }
        auto text = Tree_.TokenText(index);
        static const std::string Separator(80, '/');
        bool separator =
            text.size() >= 8 && std::ranges::all_of(text, [] (char character) {
                return character == '/';
            });
        Write(separator ? std::string_view(Separator) : text);
        if (text.starts_with("//") || text.contains('\n') ||
            index + 1 == Tree_.TokenCount() ||
            Tree_.TokenPosition(index + 1).Line
                > Tree_.TokenPosition(index).Line)
        {
            bool hasPendingBraceInsertion =
                BraceInsertionIndex_ < BraceInsertions_.size() &&
                BraceInsertions_[BraceInsertionIndex_].Index <= index + 1;
            if (trailing && !hasPendingBraceInsertion &&
                index + 1 < Tree_.TokenCount() &&
                Tree_.TokenPosition(index + 1).Line
                    > Tree_.TokenPosition(index).Line)
            {
                NextLineAbsoluteIndent_ =
                    Tree_.TokenPosition(index + 1).Column - 1;
            }
            Newline();
        } else if (Tree_.Kind(index + 1) != TK::Operator_CloseParenthesis &&
            Tree_.Kind(index + 1) != TK::Operator_CloseBracket &&
            Tree_.Kind(index + 1) != TK::Operator_Comma &&
            Tree_.Kind(index + 1) != TK::Operator_Semicolon)
        {
            Space();
        }
    }

    void FormatDirective(TokenIndex index)
    {
        if (!AtLineStart_) {
            Newline();
        }
        auto savedIndent = std::exchange(Indent_, 0);
        auto savedContinuation = std::exchange(ContinuationIndent_, 0);
        NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
        auto text = Tree_.TokenText(index);
        Write(text);

        auto lastNewline = text.rfind('\n');
        bool endsWithContinuedLine = lastNewline != std::string_view::npos &&
            std::ranges::all_of(
                text.substr(lastNewline + 1),
                [] (char character) {
                    return character == ' ' ||
                        character == '\t' ||
                        character == '\r' ||
                        character == '\f' ||
                        character == '\v';
                });
        if (endsWithContinuedLine &&
            Tree_.WhitespaceAfter(index).contains('\n'))
        {
            BlankLine();
        } else {
            Newline();
        }
        Indent_ = savedIndent;
        ContinuationIndent_ = savedContinuation;
    }

    bool PlacesBraceOnNewLine(
        BracePlacement placement,
        bool sourceBreak) const
    {
        switch (placement) {
        case BracePlacement::Preserve:
            return sourceBreak;
        case BracePlacement::Attach:
            return false;
        case BracePlacement::NewLine:
            return true;
        case BracePlacement::AttachUnlessMultiline:
            return sourceBreak;
        }
        return sourceBreak;
    }

    bool HasMultilineControlHeader(TokenIndex index) const
    {
        if (index == 0 ||
            Tree_.Kind(index - 1) != TK::Operator_CloseParenthesis)
        {
            return false;
        }
        auto opening = FindOpeningParenthesis(index - 1);
        if (opening != NoToken &&
            OutputPositions_[opening].Line != NoToken &&
            OutputPositions_[index - 1].Line != NoToken &&
            OutputPositions_[opening].Line != OutputPositions_[index - 1].Line)
        {
            return true;
        }
        if (LastControlKeyword_ != NoToken &&
            OutputPositions_[LastControlKeyword_].Line != NoToken &&
            OutputPositions_[LastControlKeyword_].Line != OutputLine_)
        {
            return true;
        }
        if (opening == NoToken) {
            return false;
        }
        if (opening != 0) {
            auto header = opening - 1;
            if (Tree_.TokenText(header) == "constexpr" && header != 0 &&
                Tree_.Kind(header - 1) == TK::Keyword_if)
            {
                --header;
            }
            if (OutputPositions_[header].Line != NoToken &&
                OutputPositions_[header].Line != OutputLine_)
            {
                return true;
            }
        }
        for (auto cursor = opening + 1; cursor < index - 1; ++cursor) {
            if (Tree_.Kind(cursor) == TK::Operator_OpenBrace &&
                Braces_[cursor] == BraceKind::Block &&
                Pair_[cursor] != NoToken && Pair_[cursor] != cursor + 1)
            {
                return true;
            }
        }
        return false;
    }

    bool OpeningBraceOnOwnLine(TokenIndex index, BraceKind kind) const
    {
        bool sourceBreak = Tree_.WhitespaceBefore(index).contains('\n');
        switch (kind) {
        case BraceKind::Function:
        case BraceKind::ConstructorFunction:
            return PlacesBraceOnNewLine(Style_.FunctionBracePlacement, sourceBreak);
        case BraceKind::Record:
        case BraceKind::Enum: {
            bool inlineRecord = kind == BraceKind::Record &&
                TokenFlags_[index].InlineRecord && !Style_.BreakInlineRecordDeclarations;
            return !inlineRecord && PlacesBraceOnNewLine(Style_.RecordBracePlacement, sourceBreak);
        }
        case BraceKind::Lambda: {
            bool multilineHeader = index != 0 &&
                Tree_.Kind(index - 1) == TK::Operator_CloseParenthesis &&
                Pair_[index - 1] != NoToken &&
                Tree_.TokenPosition(Pair_[index - 1]).Line != Tree_.TokenPosition(index - 1).Line;
            return PlacesBraceOnNewLine(Style_.LambdaBracePlacement, sourceBreak && multilineHeader);
        }
        case BraceKind::Namespace:
            return PlacesBraceOnNewLine(Style_.NamespaceBracePlacement, sourceBreak);
        case BraceKind::Block:
        case BraceKind::CaseBlock:
        case BraceKind::UnindentedBlock: {
            bool multilineHeader = HasMultilineControlHeader(index);
            bool preferredBreak = Style_.ControlBracePlacement == BracePlacement::AttachUnlessMultiline
                ? multilineHeader
                : sourceBreak || multilineHeader;
            return PlacesBraceOnNewLine(Style_.ControlBracePlacement, preferredBreak);
        }
        default:
            return false;
        }
    }

    void FormatOpeningBrace(TokenIndex index)
    {
        auto kind = Braces_[index];
        auto closing = Pair_[index];
        if (index != 0 &&
            Tree_.Kind(index - 1)
                == TK::Operator_CloseParenthesis)
        {
            auto controlOpening = FindOpeningParenthesis(index - 1);
            if (controlOpening != NoToken && controlOpening != 0) {
                auto header = controlOpening - 1;
                bool constexprIf = Tree_.TokenText(header) == "constexpr" &&
                    header != 0 &&
                    Tree_.Kind(header - 1) == TK::Keyword_if;
                if (IsControlKeyword(Tree_.Kind(header)) || constexprIf) {
                    kind = BraceKind::Block;
                    Braces_[index] = kind;
                    if (closing != NoToken) {
                        Braces_[closing] = kind;
                    }
                }
            }
        }
        bool empty = closing == index + 1;
        bool compactEmpty = Style_.KeepEmptyBlocksOnOneLine && empty && (kind == BraceKind::Function ||
            kind == BraceKind::ConstructorFunction ||
            kind == BraceKind::Record || kind == BraceKind::Enum ||
            kind == BraceKind::Lambda ||
            kind == BraceKind::CaseBlock);

        if (kind == BraceKind::Record && InBaseList_) {
            InBaseList_ = false;
            if (ContinuationIndent_ != 0) {
                --ContinuationIndent_;
            }
        }
        if (kind == BraceKind::ConstructorFunction) {
            InConstructorInitializers_ = false;
            ContinuationIndent_ = 0;
        } else if (kind == BraceKind::Function && InConstructorInitializers_) {
            InConstructorInitializers_ = false;
            if (ContinuationIndent_ != 0) {
                --ContinuationIndent_;
            }
        }

        if (OpeningBraceOnOwnLine(index, kind)) {
            Newline();
        } else if (index != 0 && NeedsSpace(index - 1, index)) {
            Space();
        }
        Write(Tree_.TokenText(index));

        if (Style_.AllowShortLambdasOnOneLine &&
            kind == BraceKind::Lambda && closing != NoToken && !empty &&
            Tree_.TokenPosition(index).Line
                == Tree_.TokenPosition(closing).Line &&
            Column_ + FlatWidth(index, closing + 1) <= Style_.ColumnLimit)
        {
            size_t semicolons = 0;
            for (auto i = index + 1; i < closing; ++i) {
                semicolons += Tree_.Kind(i) == TK::Operator_Semicolon;
            }
            if (semicolons <= 1) {
                TokenFlags_[index].CompactLambda = true;
                TokenFlags_[closing].CompactLambda = true;
                PushLayout(index, false, false, false, false);
                Space();
                return;
            }
        }

        if (kind == BraceKind::Initializer) {
            auto multiline = ShouldBreakDelimiter(index);
            bool existingMultiline = closing != NoToken &&
                Tree_.TokenPosition(index).Line
                    != Tree_.TokenPosition(closing).Line;
            bool closingOwnLine = closing != NoToken &&
                Tree_.WhitespaceBefore(closing).contains('\n');
            PushLayout(
                index,
                multiline,
                multiline && !existingMultiline,
                closingOwnLine,
                multiline);
            if (multiline) {
                ++ContinuationIndent_;
                if (!existingMultiline ||
                    Tree_.WhitespaceAfter(index).contains('\n'))
                {
                    if (existingMultiline &&
                        index + 1 < Tree_.TokenCount())
                    {
                        NextLineAbsoluteIndent_ =
                            ShiftedSourceIndent(Layouts_.back(), index + 1);
                    }
                    Newline();
                }
            }
            return;
        }

        bool indents = (kind != BraceKind::Namespace || Style_.IndentNamespaces) &&
            kind != BraceKind::UnindentedBlock &&
            kind != BraceKind::CaseBlock;
        PushLayout(
            index,
            !compactEmpty,
            kind == BraceKind::Enum,
            false,
            indents);
        if (compactEmpty) {
            Space();
            return;
        }
        if (indents) {
            ++Indent_;
            if (kind == BraceKind::Lambda) {
                auto computedIndent = static_cast<std::ptrdiff_t>(
                    EffectiveIndent() * Style_.IndentWidth);
                auto capture = Lambdas_[index].Capture;
                auto lambdaIndent = capture != NoToken &&
                    Lambdas_[capture].Indent != NoToken
                    ? Lambdas_[capture].Indent
                    : CurrentLineIndent_;
                auto desiredIndent = static_cast<std::ptrdiff_t>(
                    lambdaIndent + Style_.IndentWidth);
                Layouts_.back().IndentOffset =
                    desiredIndent - computedIndent - LayoutIndentOffset_;
                LayoutIndentOffset_ += Layouts_.back().IndentOffset;
                NextLineAbsoluteIndent_ = static_cast<size_t>(
                    std::max<std::ptrdiff_t>(0, desiredIndent));
            }
        }
        if (kind == BraceKind::Namespace) {
            bool opensNestedNamespace = index + 1 < Tree_.TokenCount() &&
                Tree_.Kind(index + 1) == TK::Keyword_namespace;
            if (opensNestedNamespace &&
                !HasOriginalBlankLine(index + 1))
            {
                Newline();
            } else {
                BlankLine();
            }
        } else if (index + 1 < Tree_.TokenCount() &&
            Tree_.Kind(index + 1) == TK::Comment &&
            Tree_.TokenPosition(index).Line
                == Tree_.TokenPosition(index + 1).Line)
        {
            Space();
        } else if (index + 1 < Tree_.TokenCount() &&
            HasOriginalBlankLine(index + 1))
        {
            BlankLine();
        } else {
            Newline();
        }
    }

    void FormatClosingBrace(TokenIndex index)
    {
        DiscardLayoutsClosedInPreservedRanges(index);
        auto opening = Pair_[index];
        auto kind = Braces_[index];
        bool hasLayout = !Layouts_.empty() && Layouts_.back().Closing == index;
        bool multiline = hasLayout && Layouts_.back().Multiline;
        bool closingOwnLine = hasLayout && Layouts_.back().ClosingOwnLine;
        bool indents = hasLayout && Layouts_.back().Indents;

        if (kind == BraceKind::Lambda && TokenFlags_[index].CompactLambda) {
            Write(Tree_.TokenText(index));
            if (hasLayout) {
                PopLayout();
            }
            return;
        }

        if (kind == BraceKind::Initializer) {
            if (multiline && ContinuationIndent_ != 0) {
                --ContinuationIndent_;
            }
            if (closingOwnLine) {
                Newline();
            } else if (!AtLineStart_ && opening != NoToken) {
                bool space = false;
                switch (Style_.SpaceInsideBracedInitializers) {
                case BracedInitializerSpacing::Preserve:
                    space = !Tree_.WhitespaceBefore(index).empty();
                    break;
                case BracedInitializerSpacing::Never:
                    break;
                case BracedInitializerSpacing::Always:
                    space = true;
                    break;
                }
                if (space) {
                    Space();
                }
            }
            Write(Tree_.TokenText(index));
            if (hasLayout) {
                PopLayout();
            }
            return;
        }

        bool compactEmpty = Style_.KeepEmptyBlocksOnOneLine &&
            opening != NoToken && opening + 1 == index && (kind == BraceKind::Function ||
            kind == BraceKind::ConstructorFunction ||
            kind == BraceKind::Record || kind == BraceKind::Enum ||
            kind == BraceKind::Lambda ||
            kind == BraceKind::CaseBlock);
        bool outdentCaseBlock =
            !compactEmpty && kind == BraceKind::CaseBlock && Indent_ != 0;
        if (!compactEmpty) {
            if (indents && Indent_ != 0) {
                --Indent_;
            }
            if (hasLayout) {
                if (kind == BraceKind::Lambda) {
                    auto capture = Lambdas_[opening].Capture;
                    if (capture != NoToken &&
                        Lambdas_[capture].Indent != NoToken)
                    {
                        NextLineAbsoluteIndent_ = Lambdas_[capture].Indent;
                    }
                } else if (Layouts_.back().IndentOffset != 0) {
                    NextLineAbsoluteIndent_ = Layouts_.back().OpeningLineIndent;
                }
                PopLayout();
                hasLayout = false;
            }
            bool followsNestedNamespace = kind == BraceKind::Namespace &&
                index != 0 && Tree_.Kind(index - 1) == TK::Comment &&
                Tree_.TokenText(index - 1).starts_with("// namespace");
            if (outdentCaseBlock) {
                --Indent_;
            }
            if (kind == BraceKind::Namespace && !followsNestedNamespace) {
                BlankLine();
            } else {
                Newline();
            }
        }
        Write(Tree_.TokenText(index));
        if (outdentCaseBlock) {
            ++Indent_;
        }
        if (hasLayout) {
            PopLayout();
        }

        if (kind == BraceKind::Namespace &&
            !(
                index + 1 < Tree_.TokenCount() &&
                Tree_.Kind(index + 1) == TK::Comment &&
                Tree_.TokenPosition(index).Line
                    == Tree_.TokenPosition(index + 1).Line))
        {
            if (const auto* comment = NamespaceCommentFor(index)) {
                Space();
                Write(*comment);
                Newline();
                return;
            }
        }

        if (index + 1 == Tree_.TokenCount()) {
            Newline();
            return;
        }
        auto next = Tree_.Kind(index + 1);
        if (kind == BraceKind::Record && TokenFlags_[index].InlineRecord &&
            !Style_.BreakInlineRecordDeclarations &&
            next != TK::Operator_Semicolon)
        {
            Space();
            return;
        }
        if (kind == BraceKind::Requirement &&
            IsKnownBinaryOperator(index + 1))
        {
            Space();
            return;
        }
        if (next == TK::Operator_Semicolon || next == TK::Operator_Comma ||
            next == TK::Operator_OpenParenthesis ||
            next == TK::Operator_CloseParenthesis ||
            next == TK::Operator_CloseBracket ||
            next == TK::Operator_CloseBrace)
        {
            return;
        }
        if (next == TK::Keyword_else || next == TK::Keyword_catch || (next == TK::Keyword_while &&
                TokenFlags_[index + 1].DoWhileContinuation) || (next == TK::Comment &&
                Tree_.TokenPosition(index).Line
                    == Tree_.TokenPosition(index + 1).Line))
        {
            Space();
        } else if (
            (kind == BraceKind::Function ||
                kind == BraceKind::ConstructorFunction) &&
            next != TK::Comment && next != TK::PreprocessingDirective && (HasOriginalBlankLine(index + 1) ||
                OriginalNewlineCount(index + 1) == 0 || Indent_ == 0 ||
                StartsPreservedRange(index + 1)))
        {
            BlankLine();
        } else {
            Newline();
        }
    }

    void AlignAfterOpeningDelimiter(TokenIndex index)
    {
        if (index + 1 >= Tree_.TokenCount()) {
            return;
        }
        switch (Style_.OpenBracketAlignment) {
        case BracketAlignment::Preserve:
            if (Tree_.WhitespaceAfter(index).contains('\n')) {
                NextLineAbsoluteIndent_ =
                    ShiftedSourceIndent(Layouts_.back(), index + 1);
            }
            break;
        case BracketAlignment::Continuation:
            break;
        case BracketAlignment::AlignWithOpening:
            NextLineAbsoluteIndent_ = Column_;
            break;
        }
    }

    void FormatOpeningDelimiter(TokenIndex index)
    {
        if (!AtLineStart_ && Tree_.WhitespaceBefore(index).contains('\n')) {
            if (index == 0 || !IsKnownBinaryOperator(index - 1) ||
                !MakeInnermostDelimiterMultiline())
            {
                NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
            }
            Newline();
        }
        if (index != 0 && NeedsSpace(index - 1, index)) {
            Space();
        }
        Write(Tree_.TokenText(index));
        if (Tree_.Kind(index) == TK::Operator_OpenBracket &&
            TokenFlags_[index].LambdaDelimiter)
        {
            Lambdas_[index].Indent = CurrentLineIndent_;
        }
        bool regroup = CanRegroupDelimiter(index);
        auto multiline = !regroup && ShouldBreakDelimiter(index);
        auto breakElements = multiline && SupportsMultilineLayout(index);
        PushLayout(
            index,
            multiline,
            breakElements,
            false,
            multiline,
            regroup);
        if (multiline) {
            auto computedOpeningIndent = static_cast<std::ptrdiff_t>(
                EffectiveIndent() * Style_.IndentWidth);
            auto openingIndent =
                static_cast<std::ptrdiff_t>(CurrentLineIndent_);
            Layouts_.back().IndentOffset =
                openingIndent - computedOpeningIndent - LayoutIndentOffset_;
            LayoutIndentOffset_ += Layouts_.back().IndentOffset;
            ++ContinuationIndent_;
            if (!ForbidsBreakAfter(index)) {
                AlignAfterOpeningDelimiter(index);
                Newline();
            }
        }
    }

    void FormatClosingDelimiter(TokenIndex index)
    {
        DiscardLayoutsClosedInPreservedRanges(index);
        bool hasLayout = !Layouts_.empty() && Layouts_.back().Closing == index;
        bool multiline = hasLayout && Layouts_.back().Multiline;
        bool regroup = hasLayout && Layouts_.back().Regroup;
        bool closingOwnLine = hasLayout && Layouts_.back().ClosingOwnLine;
        if (multiline && ContinuationIndent_ != 0) {
            --ContinuationIndent_;
        }
        bool originalClosingBreak =
            !AtLineStart_ && Tree_.WhitespaceBefore(index).contains('\n');
        if (!regroup && (closingOwnLine || originalClosingBreak)) {
            if (hasLayout && originalClosingBreak &&
                Style_.CloseBracketAlignment
                    == ClosingBracketAlignment::Preserve)
            {
                NextLineAbsoluteIndent_ =
                    ShiftedSourceIndent(Layouts_.back(), index);
            }
            Newline();
        }
        if (!AtLineStart_ && index != 0 && NeedsSpace(index - 1, index)) {
            Space();
        }
        Write(Tree_.TokenText(index));
        if (hasLayout) {
            PopLayout();
        }
        if (TokenFlags_[index].BreakAfter) {
            Newline();
        }
    }

    void FormatComma(TokenIndex index)
    {
        bool leadingComma = !IsRegrouping() && (TokenFlags_[index].BaseSeparator ||
            TokenFlags_[index].ConstructorSeparator ||
            Tree_.WhitespaceBefore(index).contains('\n'));
        if (leadingComma) {
            bool temporaryIndent =
                !TokenFlags_[index].BaseSeparator && !TokenFlags_[index].ConstructorSeparator;
            if (temporaryIndent) {
                ++ContinuationIndent_;
            }
            if (Tree_.WhitespaceBefore(index).contains('\n')) {
                NextLineAbsoluteIndent_ =
                    Tree_.TokenPosition(index).Column - 1;
            }
            Newline();
            Write(Tree_.TokenText(index));
            if (temporaryIndent) {
                --ContinuationIndent_;
            }
            if (Tree_.WhitespaceAfter(index).contains('\n')) {
                Newline();
            } else {
                Space();
            }
            return;
        }

        Write(Tree_.TokenText(index));
        bool originalBreak = !IsRegrouping() &&
            Tree_.WhitespaceAfter(index).contains('\n');
        if (originalBreak && !Layouts_.empty() && !Layouts_.back().Multiline && (Tree_.Kind(Layouts_.back().Opening)
                    == TK::Operator_OpenParenthesis ||
                Tree_.Kind(Layouts_.back().Opening)
                    == TK::Operator_OpenBracket))
        {
            Layouts_.back().Multiline = true;
            Layouts_.back().BreakElements = true;
            ++ContinuationIndent_;
        }
        bool layoutBreak = !Layouts_.empty() &&
            Layouts_.back().BreakElements && CurrentTemplateDepth_ == 0;
        bool followedBySameLineComment = index + 1 < Tree_.TokenCount() &&
            Tree_.Kind(index + 1) == TK::Comment &&
            Tree_.TokenPosition(index).Line
                == Tree_.TokenPosition(index + 1).Line;
        bool commentLabelsNextElement = followedBySameLineComment &&
            index + 2 < Tree_.TokenCount() &&
            !Tree_.TokenText(index + 1).starts_with("//") &&
            Tree_.TokenPosition(index + 1).Line
                == Tree_.TokenPosition(index + 2).Line;
        if ((originalBreak || layoutBreak) && followedBySameLineComment &&
            !commentLabelsNextElement)
        {
            Space();
        } else if (originalBreak || layoutBreak) {
            bool initializerBreak = originalBreak && !Layouts_.empty() &&
                Braces_[Layouts_.back().Opening] == BraceKind::Initializer;
            if (originalBreak && CurrentTemplateDepth_ != 0 &&
                index + 1 < Tree_.TokenCount())
            {
                NextLineAbsoluteIndent_ =
                    Tree_.TokenPosition(index + 1).Column - 1;
            } else if (initializerBreak && index + 1 < Tree_.TokenCount()) {
                NextLineAbsoluteIndent_ =
                    ShiftedSourceIndent(Layouts_.back(), index + 1);
            } else if (originalBreak && !Layouts_.empty() &&
                !Layouts_.back().BreakElements && !initializerBreak &&
                index + 1 < Tree_.TokenCount())
            {
                NextLineAbsoluteIndent_ =
                    Tree_.TokenPosition(index + 1).Column - 1;
            } else if (originalBreak && !initializerBreak && (CurrentTemplateDepth_ != 0 || Layouts_.empty() ||
                    !Layouts_.back().BreakElements))
            {
                NextLineContinuationIndent_ = 1;
            }
            if (!Layouts_.empty() && index + 1 < Tree_.TokenCount()) {
                if (Layouts_.back().BreakElements &&
                    ForbidsBreakAfter(Layouts_.back().Opening))
                {
                    NextLineAbsoluteIndent_ =
                        Layouts_.back().OpeningLineIndent + Style_.IndentWidth;
                } else if (Style_.OpenBracketAlignment
                    == BracketAlignment::AlignWithOpening)
                {
                    auto opening = Layouts_.back().Opening;
                    NextLineAbsoluteIndent_ = OutputPositions_[opening].Column
                        + Tree_.TokenText(opening).size();
                } else if (originalBreak &&
                    Style_.OpenBracketAlignment
                        == BracketAlignment::Preserve)
                {
                    NextLineAbsoluteIndent_ = ShiftedSourceIndent(
                        Layouts_.back(),
                        index + 1);
                }
            }
            Newline();
        } else {
            Space();
        }
    }

    void FormatSemicolon(TokenIndex index)
    {
        Write(Tree_.TokenText(index));
        if (IsRegrouping()) {
            if (index + 1 < Tree_.TokenCount() &&
                Tree_.Kind(index + 1) != TK::Operator_CloseParenthesis &&
                Tree_.Kind(index + 1) != TK::Operator_Semicolon)
            {
                Space();
            }
            return;
        }
        if (index + 1 < Tree_.TokenCount() &&
            Tree_.Kind(index + 1) == TK::Comment &&
            Tree_.TokenPosition(index).Line
                == Tree_.TokenPosition(index + 1).Line)
        {
            Space();
        } else if (IsInsideParentheses()) {
            if (Tree_.WhitespaceAfter(index).contains('\n')) {
                MakeInnermostDelimiterMultiline();
                Newline();
            } else if (index + 1 >= Tree_.TokenCount() || (Tree_.Kind(index + 1) != TK::Operator_CloseParenthesis &&
                    Tree_.Kind(index + 1) != TK::Operator_Semicolon))
            {
                Space();
            }
        } else if (index != 0 && index + 1 < Tree_.TokenCount() &&
            Tree_.Kind(index - 1) == TK::Operator_CloseBrace && (Braces_[index - 1] == BraceKind::Record ||
                Braces_[index - 1] == BraceKind::Enum) &&
            Tree_.Kind(index + 1) != TK::Operator_CloseBrace &&
            Tree_.Kind(index + 1) != TK::Comment &&
            Tree_.Kind(index + 1) != TK::PreprocessingDirective)
        {
            BlankLine();
        } else {
            Newline();
        }
    }

    bool FormatStructuralColon(TokenIndex index)
    {
        auto role = Tree_.Role(index);
        if (role == PR::BaseDelimiter) {
            ++ContinuationIndent_;
            InBaseList_ = true;
            Newline();
            Write(Tree_.TokenText(index));
            Space();
            return true;
        }
        if (role == PR::ConstructorInitializerDelimiter) {
            ++ContinuationIndent_;
            InConstructorInitializers_ = true;
            Newline();
            Write(Tree_.TokenText(index));
            Space();
            return true;
        }
        if (role != PR::LabelDelimiter) {
            return false;
        }

        Write(Tree_.TokenText(index));
        bool inlineCaseBody =
            TokenFlags_[index].CaseLabelDelimiter &&
            index + 1 < Tree_.TokenCount() &&
            !Tree_.WhitespaceBefore(index + 1).contains('\n');
        if (inlineCaseBody) {
            Space();
        } else {
            Newline();
        }
        if (TokenFlags_[index].BeginIndentedCaseBody) {
            ++Indent_;
        }
        return true;
    }

    void PrepareToken(TokenIndex index, TK kind)
    {
        CurrentToken_ = index;
        if (TokenFlags_[index].EndIndentedCaseBody && Indent_ != 0) {
            --Indent_;
        }
        MaybePreserveBlankLine(index);
        if (TokenFlags_[index].DefinitionStart && index != 0 &&
            Tree_.Kind(index - 1) != TK::Operator_OpenBrace &&
            Tree_.Kind(index - 1) != TK::PreprocessingDirective &&
            Tree_.Kind(index - 1) != TK::Comment &&
            !(
                Tree_.Kind(index - 1) == TK::Operator_Colon &&
                Tree_.Role(index - 1) == PR::LabelDelimiter))
        {
            bool followsAttribute =
                Tree_.Role(index - 1) == PR::AttributeDelimiter;
            if (followsAttribute) {
                if (Tree_.WhitespaceBefore(index).contains('\n')) {
                    Newline();
                }
            } else if (HasOriginalBlankLine(index) ||
                OriginalNewlineCount(index) == 0)
            {
                BlankLine();
            } else {
                Newline();
            }
        }
        if (AtLineStart_ && index != 0
            && Tree_.Kind(index - 1) == TK::PreprocessingDirective
            && Tree_.TokenText(index - 1).starts_with("#include")
            && Tree_.Kind(index)
                != TK::PreprocessingDirective
            && !HasOriginalBlankLine(index) && !Output_.empty()
            && Output_.back() == '\n')
        {
            Output_.push_back('\n');
            ++OutputLine_;
        }
        bool followsDirective = index != 0 &&
            Tree_.Kind(index - 1) == TK::PreprocessingDirective;
        bool preserveSourceIndent =
            (TokenFlags_[index].PreserveLineIndent &&
            Tree_.WhitespaceBefore(index).contains('\n')) || (followsDirective && AtLineStart_);
        if (preserveSourceIndent || (kind == TK::Operator_OpenBracket &&
                Tree_.Role(index) == PR::AttributeDelimiter &&
                Tree_.WhitespaceBefore(index).contains('\n')))
        {
            NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
        }
    }

    bool FormatStructuralToken(TokenIndex index, TK kind)
    {
        if (kind == TK::Comment) {
            FormatComment(index);
            return true;
        }
        if (kind == TK::PreprocessingDirective) {
            FormatDirective(index);
            return true;
        }
        if (TokenFlags_[index].PreserveBreakBefore && !AtLineStart_) {
            MakeInnermostDelimiterMultiline();
            Newline();
        }

        switch (kind) {
        case TK::Operator_OpenBrace:
            FormatOpeningBrace(index);
            return true;
        case TK::Operator_CloseBrace:
            FormatClosingBrace(index);
            return true;
        case TK::Operator_OpenParenthesis:
        case TK::Operator_OpenBracket:
            FormatOpeningDelimiter(index);
            return true;
        case TK::Operator_CloseParenthesis:
        case TK::Operator_CloseBracket:
            FormatClosingDelimiter(index);
            return true;
        case TK::Operator_Comma:
            FormatComma(index);
            return true;
        case TK::Operator_Semicolon:
            FormatSemicolon(index);
            return true;
        case TK::Operator_Colon:
            return FormatStructuralColon(index);
        default:
            return false;
        }
    }

    void FormatToken(TokenIndex index)
    {
        auto kind = Tree_.Kind(index);
        PrepareToken(index, kind);
        if (!FormatStructuralToken(index, kind)) {
            FormatTextToken(index, kind);
        }
    }

    struct Continuations
    {
        bool Original = false;
        bool Member = false;
    };

    bool IsJoinedContinuation(TokenIndex index, TK kind) const
    {
        return kind == TK::Keyword_else || kind == TK::Keyword_catch || (kind == TK::Keyword_while &&
            TokenFlags_[index].DoWhileContinuation);
    }

    bool StartsOriginalContinuation(TokenIndex index, TK kind) const
    {
        if (index == 0 || AtLineStart_ || IsRegrouping() ||
            IsLogicalOperator(index) ||
            !Tree_.WhitespaceBefore(index).contains('\n'))
        {
            return false;
        }
        auto previousKind = Tree_.Kind(index - 1);
        return IsKnownBinaryOperator(index) ||
            IsKnownBinaryOperator(index - 1) || ((previousKind == TK::Operator_Less ||
            previousKind == TK::Operator_Greater) &&
            !TokenFlags_[index - 1].AngleDelimiter) ||
            previousKind == TK::Keyword_return || (kind == TK::Operator_Colon &&
            Tree_.Role(index) != PR::LabelDelimiter) || (IsWord(kind) && IsWord(previousKind) &&
            kind >= TK::Literal_UserDefinedInteger &&
            kind <= TK::Literal_String) || (previousKind == TK::Operator_Less &&
            TokenFlags_[index - 1].AngleDelimiter);
    }

    Continuations PrepareTextContinuations(TokenIndex index, TK kind)
    {
        bool suppressOpeningBreak = FollowsForbiddenBreakOpening(index);
        if (!suppressOpeningBreak && !IsRegrouping() && !AtLineStart_ &&
            index != 0 &&
            Tree_.WhitespaceBefore(index).contains('\n') && (IsUncertainContextDependentOperator(index) ||
                IsUncertainContextDependentOperator(index - 1)))
        {
            NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
            Newline();
        }

        if (!suppressOpeningBreak && !IsRegrouping() && !AtLineStart_ &&
            index != 0 && !IsLogicalOperator(index) &&
            !IsJoinedContinuation(index, kind) &&
            Tree_.WhitespaceBefore(index).contains('\n'))
        {
            // Existing line breaks can encode intentional alignment or a
            // locally preferred representation. Keep them unless a dedicated
            // rule for this token has already handled the break.
            NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
            Newline();
        }

        Continuations continuations;
        if (StartsOriginalContinuation(index, kind)) {
            bool afterTemplateOpening =
                Tree_.Kind(index - 1) == TK::Operator_Less &&
                TokenFlags_[index - 1].AngleDelimiter;
            if (afterTemplateOpening || !MakeInnermostDelimiterMultiline()) {
                ++ContinuationIndent_;
                continuations.Original = true;
            }
            NextLineAbsoluteIndent_ = Tree_.TokenPosition(index).Column - 1;
            Newline();
        }

        if (!IsRegrouping() && AtLineStart_ && IsMemberAccess(kind) &&
            !IsDesignatedInitializer(index) &&
            Tree_.WhitespaceBefore(index).contains('\n'))
        {
            NextLineAbsoluteIndent_ = ShiftedMemberIndent(index);
        }
        if (!IsRegrouping() && !AtLineStart_ &&
            ShouldBreakBeforeMemberAccess(index))
        {
            ++ContinuationIndent_;
            if (Tree_.WhitespaceBefore(index).contains('\n')) {
                NextLineAbsoluteIndent_ = ShiftedMemberIndent(index);
            }
            Newline();
            continuations.Member = true;
        }
        return continuations;
    }

    bool FormatOutdentedLabel(TokenIndex index, TK kind)
    {
        bool accessSpecifier = AtLineStart_ && index + 1 < Tree_.TokenCount() &&
            Tree_.Kind(index + 1) == TK::Operator_Colon &&
            Tree_.Role(index + 1) == PR::LabelDelimiter && (kind == TK::Keyword_public || kind == TK::Keyword_protected ||
            kind == TK::Keyword_private);
        bool caseLabel = AtLineStart_ && (kind == TK::Keyword_case || kind == TK::Keyword_default);
        bool outdentedCaseLabel =
            caseLabel && !TokenFlags_[index].IndentedCaseLabel;
        if ((!accessSpecifier && !outdentedCaseLabel) || Indent_ == 0) {
            return false;
        }
        if (accessSpecifier && index != 0 &&
            Tree_.Kind(index - 1) != TK::Operator_OpenBrace)
        {
            BlankLine();
        }
        --Indent_;
        Write(Tree_.TokenText(index));
        ++Indent_;
        return true;
    }

    TokenIndex OperandEnd(TokenIndex operatorIndex) const
    {
        auto cursor = operatorIndex + 1;
        while (cursor < Tree_.TokenCount()) {
            auto kind = Tree_.Kind(cursor);
            if (IsOpening(kind) && Pair_[cursor] != NoToken) {
                cursor = Pair_[cursor] + 1;
                continue;
            }
            if (IsClosing(kind) || kind == TK::Operator_Comma ||
                kind == TK::Operator_Semicolon || (cursor != operatorIndex + 1 &&
                    IsKnownBinaryOperator(cursor)))
            {
                break;
            }
            ++cursor;
        }
        return cursor;
    }

    bool ShouldWrapBeforeTextToken(TokenIndex index) const
    {
        if (IsLogicalOperator(index) || IsDeclaratorOperator(index) ||
            IsAssignmentToken(Tree_.Kind(index)))
        {
            return false;
        }
        if (IsKnownBinaryOperator(index)) {
            auto end = Tree_.WhitespaceAfter(index).contains('\n')
                ? index + 1
                : OperandEnd(index);
            for (auto cursor = index + 1; cursor < end; ++cursor) {
                if (Tree_.Kind(cursor) == TK::Literal_String) {
                    return false;
                }
            }
            return Column_ + 1 + FlatWidth(index, end)
                > Style_.ColumnLimit;
        }
        auto textWidth = Tree_.TokenText(index).size();
        return Tree_.Role(index) == PR::ListSeparator &&
            Column_ + 1 + textWidth > Style_.ColumnLimit;
    }

    void FormatTextToken(TokenIndex index, TK kind)
    {
        auto continuations = PrepareTextContinuations(index, kind);
        MaybePreserveBlankLine(index);
        if (FormatOutdentedLabel(index, kind)) {
            return;
        }

        bool breakAfterLogical =
            !AtLineStart_ && ShouldBreakAfterLogicalOperator(index);
        if (index != 0 && !AtLineStart_ && NeedsSpace(index - 1, index)) {
            if (ShouldWrapBeforeTextToken(index)) {
                NextLineContinuationIndent_ = 1;
                Newline();
            } else {
                Space();
            }
        }
        Write(Tree_.TokenText(index));
        if (continuations.Member) {
            --ContinuationIndent_;
        }
        if (continuations.Original) {
            --ContinuationIndent_;
        }
        if (breakAfterLogical) {
            if (!MakeInnermostDelimiterMultiline()) {
                NextLineContinuationIndent_ = 1;
            } else if (Tree_.WhitespaceAfter(index).contains('\n') &&
                index + 1 < Tree_.TokenCount() &&
                !Layouts_.empty())
            {
                NextLineAbsoluteIndent_ =
                    ShiftedSourceIndent(Layouts_.back(), index + 1);
            }
            Newline();
        }
        if (TokenFlags_[index].BreakAfter) {
            Newline();
        }
    }

    const parser::SyntaxTree& Tree_;
    const Style& Style_;
    DelimiterPairs Pair_;
    std::vector<BraceKind> Braces_;
    std::vector<TokenFlags> TokenFlags_;
    std::vector<LambdaInfo> Lambdas_;
    std::vector<OutputPosition> OutputPositions_;
    std::vector<int32_t> MemberChainIndentShift_;
    std::vector<size_t> PrefixWidth_;
    std::vector<Layout> Layouts_;
    std::vector<NamespaceComment> NamespaceComments_;
    std::vector<parser::TokenRange> PreservedRanges_;
    std::vector<BraceInsertion> BraceInsertions_;
    std::string Output_;
    std::ptrdiff_t LayoutIndentOffset_ = 0;
    size_t Indent_ = 0;
    size_t ContinuationIndent_ = 0;
    size_t Column_ = 0;
    size_t CurrentLineIndent_ = 0;
    size_t CurrentTemplateDepth_ = 0;
    size_t OutputLine_ = 0;
    size_t NextLineContinuationIndent_ = 0;
    size_t NextLineAbsoluteIndent_ = NoToken;
    size_t PreservedRangeIndex_ = 0;
    size_t BraceInsertionIndex_ = 0;
    TokenIndex CurrentToken_ = NoToken;
    TokenIndex LastControlKeyword_ = NoToken;
    bool AtLineStart_ = true;
    bool PreservedThroughEnd_ = false;
    bool InBaseList_ = false;
    bool InConstructorInitializers_ = false;
};

} // namespace

std::string Format(const parser::SyntaxTree& tree, const Style& style)
{
    return Formatter(tree, style).Run();
}

std::string Format(const parser::SyntaxTree& tree)
{
    static const Style DefaultStyle;
    return Format(tree, DefaultStyle);
}

std::string Format(util::PaddedSourceView source, const Style& style)
{
    auto tree = parser::Parse(source, [] (parser::ParseError) { });
    return Format(tree, style);
}

std::string Format(util::PaddedSourceView source)
{
    static const Style DefaultStyle;
    return Format(source, DefaultStyle);
}

} // namespace formatter
