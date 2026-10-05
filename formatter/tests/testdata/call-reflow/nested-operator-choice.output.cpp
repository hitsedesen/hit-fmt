void ThrowErrors(auto what, auto why)
{
    throw Error(Foo("Some condition") || Baz("Other condition") || Bar("Third condition")) << "Cannot do that" << what
        << "right now, because of" << why;
    throw Error(Foo("Some condition") || Baz("Other condition") || Bar("Third condition") || Ooo("Fourth condition") ||
        Boo("Some other condition")) << "Cannot do that";
}
