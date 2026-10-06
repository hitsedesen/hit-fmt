#include "config_template.hpp"

#include <formatter/config.hpp>

#include <cstddef>
#include <optional>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace hit_fmt {

namespace {

std::string_view IntegerValues(std::string_view name)
{
    if (name == "IndentWidth") {
        return "1..32";
    }
    if (name == "ColumnLimit") {
        return "0 (unlimited) or 1..10000";
    }
    if (name == "RulerLength") {
        return "0 (preserve) or 3..10000";
    }
    if (name == "NamespaceCommentSpaces") {
        return "1..32";
    }
    if (name == "MaxEmptyLines") {
        return "0..10000";
    }
    if (name == "ShortNamespaceLines") {
        return "0 (no exemption) or 1..10000";
    }
    return "0 (use IndentWidth) or 1..32";
}

void PrintStringList(
    std::ostream& output,
    const std::vector<std::string>& values)
{
    output << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            output << ", ";
        }
        output << '"';
        for (char character : values[i]) {
            if (character == '"' || character == '\\') {
                output << '\\';
            }
            output << character;
        }
        output << '"';
    }
    output << ']';
}

template <typename T> struct OptionalValue
{
    static constexpr bool IsOptional = false;
};

template <typename T> struct OptionalValue<std::optional<T>>
{
    static constexpr bool IsOptional = true;
    using Type = T;
};

template <typename T>
void PrintOption(std::ostream& output, std::string_view name, const T& value)
{
    if constexpr (std::is_same_v<
        T,
        std::optional<formatter::TrailingCommentSpaceSetting>>)
    {
        output
            << "# Values: Preserve | 0..10000 "
                "(omitted: TrailingCommentSpacing)\n";
        if (!value) {
            output << "# " << name << " = Preserve\n\n";
        } else {
            output << name << " = ";
            if (value->Count) {
                output << *value->Count;
            } else {
                output << "Preserve";
            }
            output << "\n\n";
        }
        return;
    }
    if (name == "ChainsBreak") {
        output
            << "# In control headers, chains break only when needed.\n"
                "# Even with Always, prefer non-chain breaks.\n"
                "# Lambda bodies and explicit AlwaysBreakChainRootNames keep\n"
                "# their ordinary chain policy.\n";
    }
    if (name == "CaseBlockBracePlacement") {
        output
            << "# Applies when a case contains only one compound block.\n"
                "# Mixed cases align all statements and standalone blocks.\n"
                "# Example: case 1: { work(); break; } uses this placement;\n"
                "# In case 2: { work(); } break;, braces align with break.\n";
    }
    if (name == "IndentPreprocessingDirectives") {
        output
            << "# Indent the whole directive "
                "(including # or %:) by nesting\n"
                "# depth using IndentWidth spaces; "
                "remove spaces after the marker.\n"
                "# Example with IndentWidth = 4: "
                "a nested #if becomes \"    #if\".\n"
                "# else/elif/endif align with their if.\n";
    } else if (name == "ListPacking") {
        output
            << "# Block comments after a separator move with the next item.\n"
                "# Line comments and mixed comment prefixes stay in place.\n"
                "# In a broken call: consume(alpha,\n"
                "#     /* next argument */ beta);\n";
    } else if (name == "AttributeSpaceBeforeSpecifier" ||
        name == "AttributeSpaceAfterSpecifier")
    {
        output
            << "# Outside [[...]], GNU __attribute__ and MS __declspec.\n"
                "# Interiors and punctuation retain their own policies.\n";
    } else if (name == "AttributeInteriorSpacing" ||
        name == "EmptyAttributeInteriorSpacing")
    {
        output << "# C++ [[...]] bracket interiors, not extension payloads.\n";
    } else if (name == "IndentedCallScopePairs") {
        output
            << "# Uncertain scopes retain existing line-start regions.\n"
                "# Inline endpoints do not prevent body expansion.\n";
    } else if (name == "BraceInitializerSpacing") {
        output
            << "# Gap before an initializer's {, except after a comment.\n"
                "# ArgumentCommentSpacing controls comment-to-value gaps.\n";
    } else if (name == "ArgumentCommentSpacing") {
        output
            << "# Between a call argument or braced initializer element and\n"
                "# its leading/trailing block comments, for any value kind.\n"
                "# After the last leading and before the first trailing one,\n"
                "# and between consecutive block comments in attached runs.\n"
                "# Space: consume(/* first */ /* second */ value /* note */);\n"
                "# NoSpace: consume(/* first *//* second */value/* note */);\n"
                "# Default: Preserve, keeping original horizontal spelling.\n"
                "# Choose Space explicitly to normalize horizontal gaps.\n"
                "# Generated* applies when the owning list is rebuilt.\n"
                "# Only block-to-block and block-to-argument gaps qualify.\n"
                "# Newlines, line comments, interiors, separator-facing gaps\n"
                "# and protections stay put.\n";
    } else if (name == "TemplateTypeArgumentInteriorSpacing") {
        output << "# NoSpace may join confirmed template closers as >>.\n";
    } else if (name == "ScopeResolutionSpacing") {
        output
            << "# Qualified :: edges, not the gap before leading global ::.\n";
    }
    if (name == "ClassMacroNames") {
        output
            << "# Begin:End pairs of unqualified function-like macro names.\n"
                "# Endpoints must be distinct and unique across pairs.\n"
                "# Nested classes close with their configured matching end.\n"
                "# Arguments stay intact; no expanded types are inferred.\n"
                "# Example: ClassMacroNames = [\"BEGIN_SETTER:END_SETTER\"]\n";
    }
    output << "# Values: ";
    if constexpr (std::is_enum_v<T>) {
        bool first = true;
        for (auto spelling : formatter::ConfigEnumNames<T>::Values) {
            if (!first) {
                output << " | ";
            }
            output << spelling;
            first = false;
        }
        output << "\n" << name << " = "
            << formatter::SerializeConfigEnum(value);
    } else if constexpr (std::is_same_v<T, bool>) {
        output << "false | true\n" << name << " = "
            << (value ? "true" : "false");
    } else if constexpr (std::is_same_v<T, formatter::BlankLineSetting>) {
        if (name == "MemberDefinitionBlankLines" ||
            name == "MemberDeclarationBlankLines")
        {
            output << "Inherit | ";
        }
        output << "Preserve | 0..10000\n" << name << " = ";
        if (value.Mode == formatter::BlankLineMode::Exact) {
            output << value.EmptyLines;
        } else {
            output
                << (value.Mode == formatter::BlankLineMode::Inherit
                    ? "Inherit"
                    : "Preserve");
        }
    } else if constexpr (OptionalValue<T>::IsOptional &&
        !std::is_same_v<
            T,
            std::optional<formatter::TrailingCommentSpaceSetting>>)
    {
        using Item = typename OptionalValue<T>::Type;
        output << (name == "MaxEmptyLines" ? "Preserve | " : "Inherit | ");
        if constexpr (std::is_enum_v<Item>) {
            bool first = true;
            for (auto spelling : formatter::ConfigEnumNames<Item>::Values) {
                if (!first) {
                    output << " | ";
                }
                output << spelling;
                first = false;
            }
            output << '\n' << name << " = ";
            output
                << (value ? formatter::SerializeConfigEnum(*value) : "Inherit");
        } else {
            output << IntegerValues(name) << '\n' << name << " = ";
            if (value) {
                output << *value;
            } else {
                output << (name == "MaxEmptyLines" ? "Preserve" : "Inherit");
            }
        }
    } else if constexpr (std::is_same_v<T, std::size_t>) {
        output << IntegerValues(name) << '\n' << name << " = " << value;
    } else if constexpr (std::is_same_v<
        T,
        std::vector<formatter::IncludeGroupRule>>)
    {
        output << "[] or [{Prefix = \"directory/\", Depth = 0}, ...]\n" << name
            << " = [";
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (index) {
                output << ", ";
            }
            output << "{Prefixes = ";
            PrintStringList(output, value[index].Prefixes);
            output << ", Depth = " << static_cast<int>(value[index].Depth)
                << '}';
        }
        output << ']';
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (name == "NamingNamespaceExemptions" ||
            name == "ChainedCallRootNames" ||
            name == "AlwaysBreakChainRootNames")
        {
            output << "[] or [\"qualified::name\", ...]";

        } else if (name == "IndentedCallScopePairs" ||
            name == "IndentedChainScopePairs" || name == "ClassMacroNames")
        {
            output << "[] or [\"Begin:End\", ...]";
        } else if (name == "StickyChainCallPairs") {
            output << "[] or [\"First:Second\", ...]";
        } else {
            output << "[] or [\"name\", ...]";
        }
        output << '\n' << name << " = ";
        PrintStringList(output, value);
    } else if constexpr (std::is_same_v<T, std::vector<std::regex>>) {
        output << "[] or [\"path regex\", ...]\n" << name << " = []";
    }
    output << "\n\n";
}

} // namespace

void PrintPreserveConfig(std::ostream& output)
{
    output
        << "# Preserve source break topology by "
            "default. Set Layout = Canonical\n"
            "# to normalize groups. Category Inherit values resolve once.\n"
            "# Generated* applies to properties rebuilt "
            "by their owning canonical\n"
            "# combinator (also inserted gaps), not unrelated nested edits.\n"
            "# Break = Always requires a legal owned "
            "break; Preserve/protection win.\n"
            "# EmptyBodyShape is independent of nonempty BodyShape.\n"
            "# Blank counts measure empty lines; zero "
            "retains boundary topology.\n" "# Exact counts above an explicit "
            "MaxEmptyLines cap are invalid.\n"
            "# IncludeOrder = Sort enables the following include policies.\n"
            "# IncludeMatchingHeader = First selects quoted same-stem headers\n"
            "# (.h/.hh/.hpp/.hxx/.h++) in the input file's directory.\n"
            "# Pairing is disabled for header inputs or missing filenames.\n"
            "# IncludeGrouping = Preserve retains existing blank boundaries;\n"
            "# Regroup groups by IncludeGroups rules; Directory additionally\n"
            "# subdivides each rule by its directory suffix and Depth.\n"
            "# Example: IncludeGroups = [{Prefix = \"library/\", Depth = 1}]\n"
            "# Each rule has Prefix or Prefixes, "
            "and optional Depth (default 0).\n"
            "# Depth 0 keeps the entire rule together "
            "within quote/angle groups;\n"
            "# -1 uses the full remaining directory, not the filename.\n"
            "# Values 1..127 cap components after the matched prefix.\n"
            "# With prefix library/, 1 groups library/core/a.hpp and\n"
            "# library/core/detail/b.hpp together; 2 separates them.\n"
            "# Prefixes = [\"yt/yt/\", \"yt/yt_proto/yt/\"] "
            "merges corresponding\n"
            "# suffix groups; the first prefix represents "
            "their ordering path.\n"
            "# First matching rule/prefix wins; rule order "
            "is not sort priority.\n" "# Group order uses quote/angle "
            "and IncludeDirectoryOrder instead.\n"
            "# Unmatched paths use full-directory groups under Directory.\n"
            "# Prefix = \"\" is an explicit fallback "
            "for any include spelling;\n"
            "# its Depth counts from the start of the resolved directory.\n"
            "# Preserve ignores rules. Regroup ignores Depth "
            "and puts unmatched\n"
            "# headers after configured groups. Matching/standard groups stay\n"
            "# separate. Include paths are never rewritten by group aliases.\n"
            "# Regroup/Directory span blank lines and insert one empty line\n"
            "# between groups; both conflict with MaxEmptyLines = 0.\n"
            "# IncludeDirectoryOrder = NearestFirst uses parent + child steps\n"
            "# from the input directory; ties sort by directory spelling.\n"
            "# Alphabetical omits distance. "
            "Quoted groups precede angled ones.\n"
            "# IncludePathBase = SourceDirectory resolves source-relative "
            "paths.\n"
            "# ProjectRoot requires input and include paths with the same "
            "root.\n" "# Bare quoted names and quoted ./ or ../ paths stay "
            "source-relative.\n"
            "# Paths normalize lexically only; no file lookup or spelling "
            "edit.\n"
            "# IncludeStandardHeaders = Last uses a lexical heuristic: "
            "angled\n"
            "# names containing neither / nor .h go in a final alphabetical "
            "group.\n"
            "# It is case-sensitive, not ISO detection: extensionless "
            "vendor\n"
            "# names qualify; quoted names and stdio.h do not. Preserve "
            "keeps\n"
            "# ordinary sorting. The final group follows even unmatched "
            "headers.\n"
            "# Check mode reports one include-order failure per file, "
            "not per header.\n"
            "# Other formatting, naming and width failures remain "
            "independent.\n" "# Rules are validated even when inactive; "
            "prefixes must be unique.\n"
            "# Comments, macros, conditionals, protection and mixed line "
            "endings\n"
            "# stop blocks. Hunk edits require authority over the entire "
            "block.\n" "# LineEnding Preserve uses the first newline "
            "for generated endings\n"
            "# and retains copied gap bytes, not uniform "
            "per-edge preservation.\n"
            "# LF/CRLF spell only authorized whole whitespace newline atoms;\n"
            "# no final newline insertion, token/directive "
            "conversion or cleanup.\n" "# Protected regions remain byte-exact. "
            "Directive whitespace is opt-in.\n"
            "# Option guidance: --help-config\n\n" "# Formatting options\n\n";
    const formatter::Style style;
#define PRINT_STYLE_OPTION(type, name, initial) \
    PrintOption(output, #name, style.name);
    FORMATTER_STYLE_OPTIONS(PRINT_STYLE_OPTION)
#undef PRINT_STYLE_OPTION

    output << "# Naming checks\n\n";
#define PRINT_NAMING_OPTION(category)                                     \
    PrintOption(output, #category "NameCase", formatter::NameCase::Unchecked); \
    output << "# Values: [] (disabled) or [\"prefix\", ...]\n"         \
              "# [\"\"] explicitly allows names without a prefix.\n"     \
           << #category "NamePrefixes = []\n\n";
    FORMATTER_NAMING_CATEGORIES(PRINT_NAMING_OPTION)
#undef PRINT_NAMING_OPTION

    output << "# Other configuration options\n\n";
    const formatter::Configuration config;
#define PRINT_CONFIG_OPTION(type, name, initial) \
    PrintOption(output, #name, config.name);
    FORMATTER_CONFIGURATION_OPTIONS(PRINT_CONFIG_OPTION)
#undef PRINT_CONFIG_OPTION
}

} // namespace hit_fmt
