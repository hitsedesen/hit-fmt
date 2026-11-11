#pragma once

#include "config_enum.hpp"

#include <util/types.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

struct Style;

enum class FirstArgumentAnnotation : u8
{
    // Only complete literal expressions qualify, never a literal prefix of
    // another expression. Overflow controls how qualifying arguments attach.
    // Required legal owned Always breaks and safety/protection outrank it.
    PinCompleteStringLiteral,
    // Any parsed first argument qualifies; nested syntax may still wrap.
    PinAnyArgument
};

enum class ChainParticipation : u8
{
    AllSafe,
    AnnotatedRoots
};

enum class VisualScopeLayout : u8
{
    PreserveExistingExtraLevel
};

struct MessagePinAnnotation
{
    std::string Name;
    FirstArgumentAnnotation FirstArgument = FirstArgumentAnnotation::PinCompleteStringLiteral;
    AttachedStringOverflow Overflow = AttachedStringOverflow::Wrap;
    // Find the first complete literal argument, attach its prefix according
    // to Overflow, and break before every following format argument.
    bool MessageLayout = false;
};

struct VisualScopeAnnotation
{
    std::string Begin;
    std::string End;
    VisualScopeLayout Layout = VisualScopeLayout::PreserveExistingExtraLevel;
};

struct ChainCallPairAnnotation
{
    std::string First;
    std::string Second;
};

// Configuration-lifetime metadata for syntax lowering, never for the renderer.
// Owns sorted contiguous records; no views into raw Style. Publish as const and
// reuse across files. Spans/pointers survive raw-Style mutation but not
// moving, replacing or destroying this snapshot. No mutable record access.
class LibraryAnnotations
{
public:
    [[nodiscard]] std::span<const MessagePinAnnotation> MessagePins() const;
    [[nodiscard]] const MessagePinAnnotation* FindMessagePin(std::string_view name) const noexcept;
    [[nodiscard]] ChainParticipation Chains() const noexcept;
    [[nodiscard]] bool IsAnnotatedChainRoot(std::string_view name) const noexcept;
    [[nodiscard]] std::span<const std::string> ChainRoots() const;
    [[nodiscard]] std::span<const std::string> AlwaysBreakChainRoots() const;
    [[nodiscard]] bool AlwaysBreakChainRoot(std::string_view name) const noexcept;
    [[nodiscard]] std::span<const std::string> AlwaysBreakChainCalls() const;
    [[nodiscard]] bool AlwaysBreakChainCall(std::string_view name) const noexcept;
    [[nodiscard]] bool IsBlankLineMacro(std::string_view name) const noexcept;
    [[nodiscard]] std::span<const VisualScopeAnnotation> VisualScopes() const;
    [[nodiscard]] std::span<const ChainCallPairAnnotation> ChainScopes() const;
    [[nodiscard]] std::span<const ChainCallPairAnnotation> StickyChainPairs() const;
    [[nodiscard]] bool IsStickyChainPair(std::string_view first, std::string_view second) const noexcept;
    [[nodiscard]] const VisualScopeAnnotation* FindScopeBegin(std::string_view name) const noexcept;
    [[nodiscard]] const VisualScopeAnnotation* FindScopeEnd(std::string_view name) const noexcept;

private:
    struct NameFilter
    {
        u64 Initials = 0;
        u64 Lengths = 0;

        void Add(std::string_view name) noexcept
        {
            Initials |= u64{1} << (static_cast<unsigned char>(name.front()) & 63);
            Lengths |= u64{1} << (name.size() & 63);
        }

        [[nodiscard]] bool MayContain(std::string_view name) const noexcept
        {
            // Collisions only admit the existing exact string lookup. Modulo
            // lengths retain identifiers of any supported size.
            return (Lengths & (u64{1} << (name.size() & 63))) && !name.empty() &&
                (Initials & (u64{1} << (static_cast<unsigned char>(name.front()) & 63)));
        }
    };

    NameFilter MessagePinFilter_;
    NameFilter ChainRootFilter_;
    NameFilter AlwaysBreakChainRootFilter_;
    NameFilter AlwaysBreakChainCallFilter_;
    NameFilter BlankLineMacroFilter_;
    u64 BlankLinePrefixInitials_ = 0;
    std::vector<MessagePinAnnotation> MessagePins_;
    std::vector<std::string> ChainRoots_;
    std::vector<std::string> AlwaysBreakChainRoots_;
    std::vector<std::string> AlwaysBreakChainCalls_;
    std::vector<std::string> BlankLineMacros_;
    std::vector<std::string> BlankLineMacroPrefixes_;
    std::vector<VisualScopeAnnotation> VisualScopes_;
    std::vector<ChainCallPairAnnotation> ChainScopes_;
    std::vector<ChainCallPairAnnotation> StickyChainPairs_;
    // Integer indices remain valid across snapshot copies/moves; no self-views.
    std::vector<us> ScopeEndOrder_;
    friend LibraryAnnotations CompileLibraryAnnotations(const Style& style);
};

// Validates identifiers, qualified chain roots, fixed Begin:End pairs and
// duplicate/ambiguous endpoints. Throws invalid_argument, including for raw
// programmatic input. Uses bounded metadata scratch, not per-token matchers.
void ValidateLibraryAnnotationStyle(const Style& style);

// Explicit compilation after final overrides, like CompilePolicies. Own the
// result separately: mutable Configuration never caches a derived snapshot.
[[nodiscard]] LibraryAnnotations CompileLibraryAnnotations(const Style& style);

} // namespace formatter
