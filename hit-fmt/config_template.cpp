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
    if (name == "RemoveNullStatements" || name == "RemoveNullDeclarations") {
        output
            << "# Remove only beside known syntax or sequence boundaries.\n"
                "# Unknown syntax and macro bodies retain semicolons.\n"
                "# Example: struct CLASS_NAME(Example) {}; keeps its ';'.\n";
    }
    if (name == "CallLineFirstStringOverflow") {
        output
            << "# Complete string first arguments of configured names.\n"
                "# Applies to CallLineFirstStringNames only.\n"
                "# Wrap (default): ordinary wrapping between literal pieces.\n"
                "# BreakBefore: move below the opener if the whole string\n"
                "# sequence and its same-line punctuation do not fit.\n"
                "# KeepInline: keep the first piece beside the opener, even\n"
                "# over ColumnLimit; later concatenated pieces may wrap.\n"
                "# Never splits literal contents. Always and protection win.\n"
                "# Preserved call layout is unchanged. Example:\n"
                "# CallLineFirstStringNames = [\"message\"]\n"
                "# BreakBefore moves the string in\n"
                "# message(\"first \" \"second\", value) below the opener\n"
                "# if the complete first argument will not fit beside it.\n";
    }
    if (name == "IfConditionsPacking") {
        output
            << "# Fill keeps ordinary expression packing (default).\n"
                "# VerticalWhenBroken splits all top-level &&/|| operands\n"
                "# when an if header wraps, including nested call breaks.\n"
                "# Parentheses, init-statements and other controls keep\n"
                "# their own policies. Canonical header/expression layout\n"
                "# is required; fitting headers stay flat unless forced.\n"
                "# Comment attachments and protected gaps take precedence.\n"
                "# BinaryOperatorSide selects leading/trailing operators.\n"
                "# ControlHeadersFirstItem selects InlineIfFits/NextLine.\n"
                "# Example with Leading: if (ready\n"
                "#     && (left || right)\n" "#     && enabled)\n";
    }
    if (name == "ControlHeadersFirstItem") {
        output
            << "# In broken headers: InlineIfFits keeps the first condition\n"
                "# beside the opener if it fits; NextLine moves it below.\n";
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
            << "# Case and ordinary labels delimit independent sections.\n"
                "# Only complete pairs inside one section affect indentation.\n"
                "# Uncertain unlabeled scopes retain line-start regions.\n"
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
    if (name == "RemoveNullDeclarations") {
        output
            << "# Remove redundant null declarations.\n"
                "# Required semicolons stay intact.\n"
                "# Same-line namespace suffixes are removed beside comments:\n"
                "# namespace N {}; // note becomes namespace N {} // note.\n"
                "# Standalone commented placeholders stay intact.\n";
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
        output
            << "[] or [{Prefix = \"directory/\", Depth = 127, "
                "Priority = 0}, ...]\n" << name << " = [";
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (index) {
                output << ", ";
            }
            output << "{Prefixes = ";
            PrintStringList(output, value[index].Prefixes);
            output << ", Depth = " << static_cast<int>(value[index].Depth)
                << ", Priority = " << static_cast<int>(value[index].Priority)
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
            "# Each rule has Prefix or Prefixes, optional Depth and Priority.\n"
            "# Depth defaults to 127; Priority defaults to 0.\n"
            "# Priority is an integer from -128 to 127.\n"
            "# Lower values sort first within each quote/angle category.\n"
            "# Unmatched headers have priority zero.\n"
            "# Priority never changes first-match selection; equal priorities\n"
            "# use IncludeGroupOrder and the usual directory/header ordering.\n"
            "# Depth 0 keeps the entire rule together "
            "within quote/angle groups;\n"
            "# 127 uses the full remaining directory, not the filename.\n"
            "# Values 1..126 cap components after the matched prefix.\n"
            "# Depth must be an integer from 0 to 127; negatives are invalid.\n"
            "# With prefix library/, 1 groups library/core/a.hpp and\n"
            "# library/core/detail/b.hpp together; 2 separates them.\n"
            "# Prefixes = [\"yt/yt/\", \"yt/yt_proto/yt/\"] "
            "merges corresponding\n"
            "# suffix groups; the first prefix represents "
            "their ordering path.\n"
            "# First matching rule/prefix wins. IncludeGroupOrder = Directory\n"
            "# (default) orders equal-priority groups together by directory.\n"
            "# Configured uses list order within each priority tier, then\n"
            "# IncludeDirectoryOrder for subgroups within each rule.\n"
            "# Unmatched paths follow configured rules in the same tier.\n"
            "# Prefixes within one rule are aliases, not separate priorities.\n"
            "# Example: IncludeGroupOrder = Configured with IncludeGroups =\n"
            "# [{Prefix = \"library/\", Depth = 1}, "
            "{Prefix = \"util/\", Depth = 1}]\n"
            "# puts library/ first, then util/, with nearest subgroups and\n"
            "# alphabetical include paths inside each subgroup by default.\n"
            "# Unmatched paths use full-directory groups under Directory.\n"
            "# Prefix = \"\" is an explicit fallback "
            "for any include spelling;\n"
            "# its Depth counts from the start of the resolved directory.\n"
            "# Example: nearest project groups, then library/, then util/:\n"
            "# IncludeGroupOrder = Directory\n"
            "# IncludePathBase = ProjectRoot\n" "# IncludeGroups = [\n"
            "#     {Prefix = \"library/\", Priority = 1},\n"
            "#     {Prefix = \"util/\", Priority = 2},\n"
            "#     {Prefix = \"\", Depth = 3}\n" "# ]\n"
            "# Specific rules precede the fallback for matching, not output.\n"
            "# Preserve ignores rules, priorities and IncludeGroupOrder.\n"
            "# Regroup ignores Depth; unmatched headers follow configured\n"
            "# groups in the same priority tier. Paired/standard groups stay\n"
            "# separate. Include paths are never rewritten by group aliases.\n"
            "# Regroup/Directory span blank lines and insert one empty line\n"
            "# between groups; both conflict with MaxEmptyLines = 0.\n"
            "# IncludeDirectoryOrder = NearestFirst uses parent + child steps\n"
            "# from the input directory; ties sort by directory spelling.\n"
            "# Alphabetical omits distance. "
            "Quoted groups precede angled ones.\n"
            "# IncludeInGroupOrder = Alphabetical retains in-group ordering.\n"
            "# NearestFirst uses full resolved containing-directory distance,\n"
            "# then alphabetical include spelling, without changing groups.\n"
            "# Depth and aliases affect group keys, not in-group distance.\n"
            "# This also works within adjacent blocks under Preserve.\n"
            "# Paired and final standard-header groups remain alphabetical.\n"
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
