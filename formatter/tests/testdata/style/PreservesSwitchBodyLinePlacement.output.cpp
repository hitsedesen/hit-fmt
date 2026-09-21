void Select(int kind, int mode)
{
    switch (kind) {
        case 1: {
            UseFirst();
            break;
        }
        default: return;
    }

    switch (mode) {
    case 1: {
        UseFirst();
        break;
    }
    default: return;
    }
}
