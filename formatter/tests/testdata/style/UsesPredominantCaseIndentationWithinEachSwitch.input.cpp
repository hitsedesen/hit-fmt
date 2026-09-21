void Select(int kind, int mode)
{
    switch (kind) {
    case 1:
        UseFirst();
        break;
        case 2:
            UseSecond();
            break;
        default:
            UseDefault();
            break;
    }

    switch (mode) {
        case 1:
            UseFirst();
            break;
    case 2:
        UseSecond();
        break;
    default:
        UseDefault();
        break;
    }
}
