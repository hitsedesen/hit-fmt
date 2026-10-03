unsigned Combine(unsigned value)
{
    if ((value == 0xe0 && ReadUnit(1) < 0xa0) || (value == 0xed && ReadUnit(1) > 0x9f)) {
        return ((ReadUnit(0) & 0x07) << 18) | ((ReadUnit(1) & 0x3f) << 12) | ((ReadUnit(2) & 0x3f) << 6) |
            (ReadUnit(3) & 0x3f);
    }
    while ((value == 0xf0 && ReadUnit(1) < 0x90) || (value == 0xf4 && ReadUnit(1) > 0x8f)) {
        --value;
    }
    do {
        --value;
    } while ((value == 0xe0 && ReadUnit(1) < 0xa0) || (value == 0xed && ReadUnit(1) > 0x9f));
    for (int index = 0; (value == 0xe0 && ReadUnit(index) < 0xa0) || (value == 0xed && ReadUnit(index) > 0x9f); ++index)
    {
        Update(value);
    }
    if (HasPreviousProcessingStateForCurrentRequest() && HasCurrentProcessingStateForCurrentRequest() &&
        HasPendingProcessingStateForCurrentRequest())
    {
        return value;
    }
    if (ShortCheck() || AnotherShortCheck()) {
        return value;
    }
    return 0;
}
