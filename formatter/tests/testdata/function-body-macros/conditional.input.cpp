void Control(Type& ) noexcept {
auto* pointer =& object;
Sink(reinterpret_cast<const char* >(& object));
}

OpenScope
#if FEATURE
DEFINE_CASE(Suite, Pointer) {
auto* pointer =& object;
Sink(reinterpret_cast<const char* >(& object));
auto & reference = object;
auto && forwarding = object;
auto product = left * right;
auto bits = left & right;
}
void Other(Type& ) noexcept {}
#else
DEFINE_METHOD(Alternative, void) {
auto * pointer = & object;
Sink(static_cast<const Type* >(& object));
}
#endif
CloseScope

ScopeBegin
// Conditional declarations.
#if FEATURE
auto * pointer = & object;
auto & reference = object;
#endif
ScopeEnd

ScopeBegin
#if OUTER
#if FEATURE
void NamedCast() {
Sink(reinterpret_cast<const char* >(& object));
}
#endif
#endif
ScopeEnd

ScopeBegin
#if FEATURE
inline void Other(Type& ) noexcept {}
#endif
ScopeEnd
