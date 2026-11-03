REGISTER_HANDLER("/synthetic", [](const NExample::THandlerDependencies& deps) {
    return std::make_shared<NExample::TVeryLongDescriptiveHandlerClassName>("/synthetic", deps.RpcClient, deps.HandlersSettings.GetDescriptiveName());
})
