void Receive()
{
    transfer->Run(std::move(chunks))
        .AsUnique()
        .Subscribe(Bind([
            sink = std::move(sink),
            prefix = std::move(prefix),
            channel = std::move(channel),
            requestId
        ] (TErrorOr<std::vector<TSharedRef>>&& result) mutable {
            if (!result.IsOK()) {
                // A failed transfer has nothing to send.
                return;
            }

            // Combine the initial data with the transferred data.
            auto& data = result.Value();
            TSharedRefArrayBuilder builder(prefix.Size() + data.size());
            for (const auto& part : prefix) {
                builder.Add(part);
            }
            for (auto& part : data) {
                builder.Add(std::move(part));
            }

            sink->Send(builder.Finish(), std::move(channel), nullptr, requestId);
        }));
}
