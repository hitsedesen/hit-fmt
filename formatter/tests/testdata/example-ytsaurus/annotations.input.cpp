void Emit(){
    YT_LOG_INFO("Packet is ready", firstLongArgumentForTheCurrentEmissionContext, secondLongArgumentForTheCurrentEmissionContext);
    YT_LOG_INFO("Packet " + suffix, firstLongArgumentForTheCurrentEmissionContext, secondLongArgumentForTheCurrentEmissionContext);
    writer.OnBeginList();
        writer.Item(1);
    writer.OnEndList();
    writer.OnBeginMap();
    writer.Item(2);
    writer.OnEndMap();
}
