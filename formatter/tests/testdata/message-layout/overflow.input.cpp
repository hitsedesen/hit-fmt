void Report() {
raise(Kind() == peer.Kind(), "Entries differ in batch %v", batch);
receiver.raise(Kind() == peer.Kind(), "Entries differ in batch %v", batch);
raise(Kind() == peer.Kind(), "Incompatible entries in the requested batch %v", batch);
raise(Kind() == peer.Kind(), "Incompatible entries in the requested batch");
raise(ready, code, "Incompatible entries in the requested batch %v", batch);
raise(ReadCurrentDescriptor() == peer.ReadCurrentDescriptor(), "Invalid %v", value);
raise(LoadCurrentDescriptorMetadata() == peer.LoadCurrentDescriptorMetadata(), "Invalid %v", value);
raise(ready && valid, "Incompatible entries in the requested batch %v", batch);
message(code, "A description with " "two separately stored message fragments %v", value);
message("An indivisible message longer than the configured maximum line width %v", value);
raise(ready, "Valid %v", value);
THROW_ERROR_EXCEPTION_IF(left.ComputeCurrentValue() != right.ComputeCurrentValue(), "The computed values must match");
raise(ready, code, text, firstArgument, secondArgument, thirdArgument);
raise(ready, "Outer %v", message(code, "Nested %v", value));
raise(message(code, "Nested %v", value), "Outer %v", other);
raise(/* condition */ ready, "A message longer than the configured line width %v", /* value */ value);
raise(ready, // condition must stay terminated
"A message longer than the configured line width %v", value);
// hit-fmt off
raise(  ready, "Protected long message that must preserve its layout %v", value);
// hit-fmt on
}
