void WriteSplit(int tag)
{
    switch (tag) {
    case 0:
        encoder.OnBeginList();
        encoder.Item(1);
    case 1:
        encoder.Item(2);
        encoder.OnEndList();
        break;
    default:
        Finish();
    }
}

void WriteUnclosed(int tag)
{
    switch (tag) {
    case 0:
        Prepare();
        encoder.OnBeginMap();
        if (Ready()) {
            encoder.Item(3);
        }
    default:
        encoder.Item(4);
    }
}

void WriteUnopened(int tag)
{
    switch (tag) {
    case 0:
        encoder.Item(5);
        break;
    default:
        encoder.OnEndMap();
    }
}

void WriteMatched(int tag)
{
    switch (tag) {
    case 0:
        encoder.OnBeginList();
        encoder.Item(6);
        encoder.OnEndList();
        break;
    case 1: {
            encoder.OnBeginMap();
                encoder.Item(7);
            encoder.OnEndMap();
            break;
        }
    default:
        Finish();
    }
}

void WriteLabeled()
{
    wrap(tag, encoder.OnBeginList());
    Before();
    next:
    After();
    wrap(tag, encoder.OnEndList());
    Unrelated();
    done:
    wrap(tag, encoder.OnBeginMap());
        Within();
    wrap(tag, encoder.OnEndMap());
}

void WriteShifted(int tag)
{
    switch (tag) {
    case 0:
        encoder.OnBeginList();
        encoder.Item(8);
    case 1:
        encoder.OnEndList();
    }
}
