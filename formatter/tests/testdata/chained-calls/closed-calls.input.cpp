void assemble() {
    pipeline->Apply(BuildPlan().Read(SourcePath(stagedRecords.Name()).Slice(RowSpan::FromOffsets(0, static_cast<long long>(settings.MaximumRecordCount)))).Write(DestinationPath(finalStoragePath).OrderBy({"Primary", "Secondary"})).Mode(StableMode));
    pipeline->Apply(BuildPlan().Read(SourcePath(stagedRecords.Name())
        .Slice(
            RowSpan::FromOffsets(0,
                static_cast<long long>(settings
                    .MaximumRecordCount)))).Write(DestinationPath(finalStoragePath)
                        .OrderBy({"Primary", "Secondary"})).Mode(StableMode));
}
