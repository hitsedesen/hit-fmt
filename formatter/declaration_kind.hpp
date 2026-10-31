#pragma once

#include <parser/syntax_tree.hpp>

namespace formatter {

inline bool DeclarationItem(parser::NodeKind kind)
{
    switch (kind) {
        case parser::NodeKind::MacroEnumDeclaration:
        case parser::NodeKind::EmptyDeclaration:
        case parser::NodeKind::Declaration:
        case parser::NodeKind::FunctionDeclaration:
        case parser::NodeKind::FunctionDefinition:
        case parser::NodeKind::MacroFunctionDefinition:
        case parser::NodeKind::FunctionTryBlock:
        case parser::NodeKind::NamespaceDeclaration:
        case parser::NodeKind::RecordDeclaration:
        case parser::NodeKind::MacroRecordDeclaration:
        case parser::NodeKind::EnumDeclaration:
        case parser::NodeKind::TemplateDeclaration:
        case parser::NodeKind::UsingDeclaration:
        case parser::NodeKind::TypedefDeclaration:
        case parser::NodeKind::StaticAssertDeclaration:
        case parser::NodeKind::ConceptDeclaration:
        case parser::NodeKind::ModuleDeclaration:
        case parser::NodeKind::ImportDeclaration:
        case parser::NodeKind::ExportDeclaration:
        case parser::NodeKind::AsmDeclaration:
        case parser::NodeKind::LinkageSpecification:
            return true;
        default:
            return false;
    }
}

} // namespace formatter
