bool function()
{
    bool matches = expression->as<AliasNode>()->nestedExpression->as<ReferenceNode>()->reference.memberName ==
        firstExpectedMemberName;
    return matches;
}
