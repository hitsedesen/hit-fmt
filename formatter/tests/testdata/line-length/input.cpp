void CountMessages(int value)
{
    auto total = ReadCountFromPrimaryBucket(value) + ReadCountFromBackupBucket(value);
    auto unbreakable = IdentifierWhoseSpellingAloneIsLongerThanTheConfiguredLimit;
    auto mixed = FetchShortValue(value) + IdentifierWhoseSpellingAloneIsLongerThanTheConfiguredLimit;
    // This synthetic comment is also longer than the configured limit and must stay untouched.
    ;
}
