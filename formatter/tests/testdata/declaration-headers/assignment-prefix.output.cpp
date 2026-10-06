RecordForTransformation::ValueStorage
    maximumReservedElementCountForCurrentBatch = Make();

RecordForTransformation::ValueStorage
    Owner::maximumReservedElementCountForCurrentBatch = Make();

void Sample()
{
    RecordForTransformation::ValueStorage
        maximumReservedElementCountForCurrentBatch = Make();
    const RecordForTransformation::ValueStorage&
        maximumReservedElementCountForCurrentBatch = source;
    unsigned long long
        maximumReservedElementCountForCurrentTransformationStage = 0;
    RecordForTransformation::ValueStorage
        maximumReservedElementCountForCurrentBatch[2] = {};
    RecordForTransformation::ValueStorage
        maximumReservedElementCountForCurrentBatch =
        Make(firstLongArgument, secondLongArgument, thirdLongArgument);
    RecordForTransformation::ValueStorage shortName =
        Make(firstLongArgument, secondLongArgument, thirdLongArgument);
    RecordForTransformation::ValueStorage fitting = Make();
    BufferSize maximumEncodedByteCountForCurrentBuffer = Estimate(
        unencodedByteCountForCurrentBuffer,
        estimatedByteCountForCurrentBuffer);
    size_t maximumReservedByteCountForCurrentBuffer = Estimate(
        unreservedByteCountForCurrentBuffer,
        estimatedByteCountForCurrentBuffer);
}
