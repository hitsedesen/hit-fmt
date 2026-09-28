void WritePacket()
{
    Encoder encoder;
    encoder.OnBeginList();
        encoder.Item(1);
        encoder.Item(2);
        encoder.Item(3);
    encoder.OnEndList();
    auto* sink = GetSink();
    sink->OnBeginAttributes();
        sink->WriteKey("metadata");
        sink->OnBeginMap();
            sink->WriteKey("id");
            sink->WriteValue(7);
        sink->OnEndMap();
        sink->WriteValue(8);
    sink->OnEndAttributes();
    Notify();
        Log();
        Trace();
    other.OnBeginList();
        other.Item(4);
    encoder.OnEndList();
    EXPECT_CALL(writerMock, OnBeginList());
        EXPECT_CALL(writerMock, RecordStep());
        EXPECT_CALL(writerMock, AcceptToken(23));
        EXPECT_CALL(writerMock, RecordMetric(MatchesCount(4)));
        EXPECT_CALL(writerMock, OnBeginMap());
            EXPECT_CALL(writerMock, CaptureTag("cobalt"));
        EXPECT_CALL(writerMock, OnEndMap());
    EXPECT_CALL(writerMock, OnEndList());
    EXPECT_CALL(writerMock, OnBeginList());
    EXPECT_CALL(writerMock, RecordStep());
    EXPECT_CALL(writerMock, OnEndList());
    EXPECT_CALL(unrelatedMock, OnBeginList());
        EXPECT_CALL(unrelatedMock, RecordStep());
    EXPECT_CALL(writerMock, OnEndList());
    EXPECT_CALL(writerMock, OnBeginList());
        EXPECT_CALL(writerMock, RecordStep());
    EXPECT_CALL(writerMock, OnEndMap());
    AUDIT_CALL(writerMock, OnBeginList());
        AUDIT_CALL(writerMock, RecordStep());
    AUDIT_CALL(writerMock, OnEndList());
    AUDIT_CALL(writerMock, OnBeginList());
        AUDIT_CALL(writerMock, RecordStep());
    EXPECT_CALL(writerMock, OnEndList());
    EXPECT_CALL(writerMock, OnBeginList());
        EXPECT_CALL(writerMock, AcceptToken(31));
        EXPECT_CALL(writerMock, OnBeginMap());
        EXPECT_CALL(writerMock, CaptureTag("saffron"));
        EXPECT_CALL(writerMock, OnEndMap());
        EXPECT_CALL(writerMock, RecordStep());
    EXPECT_CALL(writerMock, OnEndList());
    TRACE_SCOPE(writerMock, 12, OnBeginList(), Guard(3));
        TRACE_SCOPE(writerMock, 12, LogStep(), Guard(3));
        TRACE_SCOPE(writerMock, 12, OnBeginMap(), Guard(4));
            TRACE_SCOPE(writerMock, 12, LogStep(), Guard(4));
        TRACE_SCOPE(writerMock, 12, OnEndMap(), Guard(4));
    TRACE_SCOPE(writerMock, 12, OnEndList(), Guard(3));
    SCOPE_LOG(OnBeginList(), writerMock, Marker(1));
        SCOPE_LOG(Note(), writerMock, Marker(1));
    SCOPE_LOG(OnEndList(), writerMock, Marker(1));
    SCOPE_LOG(writerMock, Marker(1), OnBeginList());
        SCOPE_LOG(writerMock, Marker(1), Note());
    SCOPE_LOG(writerMock, Marker(1), OnEndList());
    SCOPE_LOG(OnBeginList(), writerMock, Marker(1));
        SCOPE_LOG(Note(), writerMock, Marker(1));
    SCOPE_LOG(OnEndList(), writerMock, Marker(2));
    EXPECT_CALL(writerMock, OnBeginList());
        EXPECT_CALL(writerMock, OnBeginMap());
        EXPECT_CALL(writerMock, CaptureTag("missing"));
    EXPECT_CALL(writerMock, OnEndList());
    TRACE_SCOPE(OnBeginList());
        TRACE_SCOPE(LogStep());
    TRACE_SCOPE(OnEndList());
    TRACE_SCOPE(OnBeginList(), OnBeginMap());
        TRACE_SCOPE(LogStep());
    TRACE_SCOPE(OnEndList(), OnEndMap());
}

TEST(TScopeSuite, EmitsTokens)
{
    SessionGuard scope;
    EXPECT_CALL(writerMock, OnBeginList());
        EXPECT_CALL(writerMock, AcceptToken(47));
        EXPECT_CALL(writerMock, OnBeginMap());
        EXPECT_CALL(writerMock, CaptureTag("umber"));
        EXPECT_CALL(writerMock, OnEndMap());
    EXPECT_CALL(writerMock, OnEndList());
}

TEST(TScopeSuite, RecordsWithLocal)
{
    EXPECT_CALL(writerMock, OnBeginMap());
        EXPECT_CALL(writerMock, CaptureTag("teal"));
        int localId = 17;
        SampleBatch batch(localId, 'V');
        EXPECT_CALL(writerMock, CaptureCount(SizeOf(batch)));
    EXPECT_CALL(writerMock, OnEndMap());
}
