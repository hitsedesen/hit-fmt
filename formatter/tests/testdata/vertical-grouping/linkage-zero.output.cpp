extern "C" {
// Entry documentation.
void Entry(); // Entry tail.
int status;
// Closing comment.
}
extern "C++" { void Other(); }
extern "C" {}
extern "C" {
void Padded();
}
extern "C" {
extern "C++" {
void Nested();
}
}
extern "C" {
#if ENABLED
void Conditional();
#endif
}
extern "C" {
// hit-fmt off
void  Protected();
// hit-fmt on
}
extern "C" void Single();
void Neighbor();
