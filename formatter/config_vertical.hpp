#pragma once

#include <cstddef>
#include <cstdint>

namespace formatter {

// Member declaration/definition counts can inherit their enclosing policy.
// Keep counts wide until validating the 0..10000 range before narrowing.
enum class BlankLineMode : std::uint8_t
{
    Preserve,
    Inherit,
    Exact,
    Minimum
};

struct BlankLineSetting
{
    BlankLineMode Mode = BlankLineMode::Preserve;
    std::size_t EmptyLines = 0;

    [[nodiscard]] static constexpr BlankLineSetting Inherited() noexcept
    {
        return {BlankLineMode::Inherit, 0};
    }

    bool operator==(const BlankLineSetting& other) const = default;
};

// Concrete vertical counts measure empty lines, not newline characters.
// Positive counts require a boundary break; zero retains inline/broken status.
// Minimum retains larger source gaps, subject to the applicable empty-line cap.
enum class VerticalGapMode : std::uint8_t
{
    Preserve,
    Exact,
    Minimum
};

struct VerticalGapPolicy
{
    VerticalGapMode Mode = VerticalGapMode::Preserve;
    std::uint16_t EmptyLines = 0;
};

struct EmptyLineCap
{
    bool Enabled = false;
    std::uint16_t EmptyLines = 0;
};

struct VerticalGapPolicies
{
    VerticalGapPolicy NamespaceBlankLinesAfterOpen;
    VerticalGapPolicy NamespaceBlankLinesBeforeClose;
    VerticalGapPolicy LinkageBlankLinesAfterOpen;
    VerticalGapPolicy LinkageBlankLinesBeforeClose;
    VerticalGapPolicy AccessSectionBlankLinesBefore;
    VerticalGapPolicy AccessSectionBlankLinesAfterLabel;
    VerticalGapPolicy DeclarationBlankLines;
    VerticalGapPolicy DeclarationCategoryBlankLines;
    VerticalGapPolicy MemberDeclarationBlankLines;
    VerticalGapPolicy DefinitionBlankLines;
    VerticalGapPolicy MemberDefinitionBlankLines;
    VerticalGapPolicy CaseBlankLines;
    EmptyLineCap MaxEmptyLines;
    VerticalGapPolicy StandaloneCommentBlankLinesBefore;
    VerticalGapPolicy StandaloneCommentBlankLinesAfter;
    VerticalGapPolicy BlockBlankLinesAfter;
    VerticalGapPolicy ControlStatementBlankLinesBefore;
    VerticalGapPolicy MacroBlankLinesAround;
    EmptyLineCap MemberGroupMaxEmptyLines;
};

} // namespace formatter
