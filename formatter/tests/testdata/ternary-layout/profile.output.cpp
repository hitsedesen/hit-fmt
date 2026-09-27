EMode ResolveMode()
{
    return configuredMode.value_or(
        selectedEncoding == EEncoding::HumanReadable
            ? EMode::ReadFriendlyFormat
            : EMode::CompactBinaryFormat);
}
