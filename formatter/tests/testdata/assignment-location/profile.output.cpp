void ConfigureChannel()
{
    ChannelAddress channelAddress;
    channelAddress = ChannelAddress::FromScopedIdentifier(
        Identifiers::MakeCanonicalPath(
            *config
                .PreferredCommunicationPipeNameWithExtendedScope));
    auto unchanged = MakeMode(shortCode);
}
