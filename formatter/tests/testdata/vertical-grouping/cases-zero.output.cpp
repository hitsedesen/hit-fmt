void process(int value) {
    switch (value) {
    case 0:
    case 1:
        work();
        [[fallthrough]];
    case 2:
        other();
        [[vendor::hint]] [[fallthrough]];
    [[likely]] case 3:
        if (value) {
            work();
        }
        [[fallthrough]];
    // Shared handling.
    [[unlikely]] case 4:
    case 5: {
        work();
        break;
    }
    case 6:
        switch (value) {
        case 0:
            other();
            [[fallthrough]];
        default:
            break;
        }
        break; // Finished nested switch.
    default:
        break;
    }
    switch (value) {
    case 0: work(); [[fallthrough]]; case 1: other(); break;
    default: break;
    }
    switch (value) {
    case 0:
    // An empty label still belongs to the next case.
    case 1:
        work();
#if EXTRA
    case 2:
        other();
        [[fallthrough]];
    case 3:
        break;
#else
    case 4:
        other();
        [[fallthrough]];
    case 5:
        break;
#endif
    default:
        break;
    }
}
