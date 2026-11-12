void F() {
    const bool transient = error.IsTransportError() && !error.IsResolveError() && !error.IsAccessDenied() &&
        !error.IsUnauthorized();
}
