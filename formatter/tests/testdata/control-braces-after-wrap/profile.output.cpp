void inspect()
{
    if (meta::matches<std::unique_ptr<ScalarValue>>(entry) && !scanner.GetTail().empty()) {
        accept();
    } else if (meta::matches<OtherTag>(entry) && scanner.GetTail().empty()) {
        reject();
    }
    while (meta::matches<Tag>(entry) && !scanner.GetCurrentDocument().GetTail().empty() && canContinueProcessing()) {
        advance();
    }
    for (; meta::matches<Tag>(entry) && !scanner.GetTail().empty(); advance()) {
        accept();
    }
    if (ready()) {
        accept();
    }
    if (meta::matches<Tag>(entry)) {
        accept();
    }
    if (meta::matches<std::unique_ptr<ScalarValue>>(entry) && !scanner.GetTailForCurrentDocument().empty() &&
        canContinueProcessing())
    {
        accept();
    }
    if (isReadyForCurrentDocument() && scanner.GetCurrentDocument().GetTail().empty()) {
        accept();
    }

    if (hasAvailableInput() || validate(scanner.GetCurrentDocument().GetTail().empty())) {
        accept();
    }

    if (isReadyForCurrentDocument() && scanner.HasTailMatching(firstExtendedExpectedTail, secondExtendedExpectedTail)) {
        accept();
    }

    if (isReadyForCurrentDocument() && scanner->GetCurrentDocument().GetTail()->empty()) {
        accept();
    }

    if (event.WithDetails().IsValid()) {
        accept();
    }

    if (expectedTailForCurrentDocument != scanner.GetCurrentDocument().GetTail()) {
        accept();
    }

    for (auto current = scanner.GetCurrentDocument().GetTail(); current.IsValid(); current.Advance()) {
        accept();
    }

    switch (auto current = scanner.GetCurrentDocument().GetTail(); current.GetKind()) {
        default:
            break;
    }

    do {
        advance();
    } while (!scanner.GetCurrentDocument().GetTail().empty() && canContinueProcessing());

    if (scanner
        .GetTailForTheCurrentDocumentAndItsCompleteProcessingContext()
        .IsEmptyForTheCurrentInputAndItsOutstandingRequests())
    {
        accept();
    }

    if (validate([] {
        return builder
            .WithInput()
            .Finish();
    }))
    {
        accept();
    }

    builder
        .WithInput()
        .Finish();
}
