auto BuildPacketFluently(NWire::EWireFormat format = NWire::EWireFormat::Binary)
    -> typename TBuilderType<
        type>::template TValue<TPacketHolder<TPacketWriterState>>
{
    return {};
}
auto BuildPacketFluently(NWire::EWireFormat format = NWire::EWireFormat::Binary)
    -> typename TBuilderType<
        type>::template TValue<TPacketHolder<TPacketWriterState>>;
auto MakePacket(NWire::EWireFormat format = NWire::EWireFormat::Binary)
    -> ExceptionallyLongReturnTypeNameWithNoTemplateNestingAndNoQualifiedTypeNames;
auto MakeSmallPacket(NWire::EWireFormat format = NWire::EWireFormat::Binary)
    -> Packet;
struct PacketFactory {
    auto BuildPacketFluently(
        NWire::EWireFormat format = NWire::EWireFormat::Binary) const noexcept
        -> typename TBuilderType<
            type>::template TValue<TPacketHolder<TPacketWriterState>>;
    auto BuildPacketFluently(
        NWire::EWireFormat format = NWire::EWireFormat::Binary) const noexcept
        -> typename TBuilderType<
            type>::template TValue<TPacketHolder<TPacketWriterState>>
    {
        return {};
    }
};
auto BuildManyPackets(
    FirstParameterWithLongName first,
    SecondParameterWithLongName second,
    ThirdParameterWithLongName third) -> typename TBuilderType<
    type>::template TValue<TPacketHolder<TPacketWriterState>>;
auto MakePair(int value)
    -> Pair<FirstVeryLongReturnTypeName, SecondVeryLongReturnTypeName>;
auto ComputePacket(
    int value) -> decltype(SelectPacket<PacketHolder<PacketWriterState>>(
        value));
auto ConstrainedPacket(int value)
    -> typename TBuilderType<
    type>::template TValue<
    TPacketHolder<TPacketWriterState>> requires Ready<type>;
auto CommentedPacket(int value) // Keep the return type after the comment.
    -> typename TBuilderType<
        type>::template TValue<TPacketHolder<TPacketWriterState>>;
