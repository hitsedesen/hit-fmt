void compareViews() {
    bool equal = expectedView == source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool unequal = expectedView != source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool less = expectedView < source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool lessEqual = expectedView <= source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool greater = expectedView > source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool greaterEqual = expectedView >= source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    auto order = expectedView <=> source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool compact = expectedView != otherView;
    bool shortLeftCall = extendedViewLabel.find_first_not_of(" \t\r\n") != std::string_view::npos;
    bool shortRightCall = std::string_view::npos != extendedViewLabel.find_first_not_of(" \t\r\n");
    if (expectedView != source.slice(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset) || retryRequested) {
        reportMismatch();
    }
}
void otherOperators() {
    auto sum = baseValue + source.read(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    auto product = baseValue * source.read(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    auto bits = baseValue & source.read(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    auto shifted = baseValue << source.read(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
    bool ready = firstCondition || source.check(span.InitialSourceOffset, span.FinalSourceOffset - span.InitialSourceOffset);
}
