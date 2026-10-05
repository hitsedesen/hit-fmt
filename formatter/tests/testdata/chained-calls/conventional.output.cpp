void Events() {
    WRITE_EVENT("Ready")
        .With("Count", count);
    WRITE_EVENT("Ready");
    READ_EVENT("Ready").With("Count", count);
    WRITE_EVENT_IF("Ready").With("Count", count);
    logging::Event("Ready")
        .With("Count", count);
    logging /* Keep this qualifier. */ :: Event("Ready")
        .With("Count", count);
    Event("Ready").With("Count", count);
    WRITE_EVENT("Ready").Field;
    WRITE_EVENT("Ready")
        .With("Count", count)
        ->Finish();
    READ_EVENT("Ready").First().Second();
    // hit-fmt off
    WRITE_EVENT("Protected").With("Count", count);
    // hit-fmt on
}
