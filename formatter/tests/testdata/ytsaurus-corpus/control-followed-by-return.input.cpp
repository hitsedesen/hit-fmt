auto Execute(auto limit, auto request, auto service)
{
    if (limit) {
        request->set_limit(*limit);
    }
    return ExecuteVerb(service, request).Apply([] (auto result) {
        return result;
    });
}

auto ExecuteBound(auto limit, auto request, auto service)
{
    if (limit) {
        request->set_limit(*limit);
    }
    return ExecuteVerb(service, request).Apply(BIND([] (const TResponse& response) {
        YT_VERIFY(response.IsOK());
        return response.Value();
    }));
}

auto ExecuteOpaque(auto limit, auto request, auto service)
{
    if (limit) {
        request->set_limit(*limit);
    }
    return ExecuteVerb(service, request).Apply([] {
        return PP_STRINGIZE(unparsed tokens);
    });
}

void DispatchOpaque(auto ready)
{
    if (ready) {
        Prepare();
    }
    Consume(PP_STRINGIZE(unparsed tokens));
    Finish();
}
