void Example() {
    if (first.IsReady() && second.IsReady()
        && !value.IsEmpty())
    {
        Use();
    }
}

void Variations() {
    if (first.IsReady() && second.IsReady()
        && value.IsEmpty())
    {
        Use();
    }
    if (first.IsReady() || second.IsReady()
        || !value.IsEmpty())
    {
        Use();
    }
    if (first.IsReady() && second.IsReady()
        && value->IsEmpty())
    {
        Use();
    }
    if (ready && !value.IsEmpty()) {
        Use();
    }
}
