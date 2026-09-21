namespace sample::formatting {

////////////////////////////////////////////////////////////////////////////////

// a sentence without canonical formatting is preserved.
//! Documents #DoSomething() and |usedArg|.
void DoSomething(int usedArg, int /*unusedArg*/, bool enabled)
{
    Call(usedArg, /*enabled*/ enabled);
}

MAKE_ENUM(ValueKind,
    (One)
    (Two)
);

////////////////////////////////////////////////////////////////////////////////

} // namespace sample::formatting
