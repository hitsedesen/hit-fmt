void Visit()
{
    auto enter = [&] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool previousTrailingLabel) {
            VisitNode(node, parent, previous, previousTrailingLabel);
        };
    auto copy = [=] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool previousTrailingLabel) {
            VisitNode(node, parent, previous, previousTrailingLabel);
        };
    auto single = [previousTrailingLabel] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool trailingLabel) {
            VisitNode(node, parent, previous, trailingLabel);
        };
    auto several = [&, previousTrailingLabel] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool trailingLabel) {
            VisitNode(node, parent, previous, trailingLabel);
        };
    auto initialized = [label = MakeLabel()] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool trailingLabel) {
            VisitNode(node, parent, previous, trailingLabel);
        };
    auto empty = [] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool trailingLabel) {
            VisitNode(node, parent, previous, trailingLabel);
        };
    auto wide = [
        firstLabel = MakeFirstLabel(),
        secondLabel = MakeSecondLabel(),
        thirdLabel = MakeThirdLabel()
    ] (const Node* node, NodeKind parent, bool trailingLabel) {
        VisitNode(node, parent, thirdLabel, trailingLabel);
    };
    auto noParameters = [&, label = MakeLabel()] {
        VisitLabel(label);
    };
    auto comment = [& /* keep */] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool trailingLabel) {
            VisitNode(node, parent, previous, trailingLabel);
        };
    auto multiline = [&] (
        const Node* node,
        NodeKind parent,
        NodeKind previous,
        bool previousTrailingLabel) {
            VisitNode(node, parent, previous, previousTrailingLabel);
        };
}
