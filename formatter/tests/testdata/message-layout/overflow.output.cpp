void Report()
{
    raise(Kind() == peer.Kind(), "Entries differ in batch %v",
        batch);
    receiver.raise(
        Kind() == peer.Kind(), "Entries differ in batch %v",
        batch);
    raise(
        Kind() == peer.Kind(),
        "Incompatible entries in the requested batch %v",
        batch);
    raise(
        Kind() == peer.Kind(),
        "Incompatible entries in the requested batch");
    raise(
        ready,
        code,
        "Incompatible entries in the requested batch %v",
        batch);
    raise(
        ReadCurrentDescriptor() == peer.ReadCurrentDescriptor(),
        "Invalid %v",
        value);
    raise(
        LoadCurrentDescriptorMetadata() ==
            peer.LoadCurrentDescriptorMetadata(),
        "Invalid %v",
        value);
    raise(
        ready && valid,
        "Incompatible entries in the requested batch %v",
        batch);
    message(
        code,
        "A description with "
            "two separately stored message fragments %v",
        value);
    message(
        "An indivisible message longer than the configured maximum line width %v",
        value);
    raise(ready, "Valid %v",
        value);
}
