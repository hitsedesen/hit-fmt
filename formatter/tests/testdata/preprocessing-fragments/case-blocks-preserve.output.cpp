int SelectPlatform(int value)
{
    switch (value) {
#ifdef PLATFORM_SUPPORTED
        case 0: {
            return 0;
        }
#endif
        case 1:
        case 2: {
            return 1;
        }

        case 3:
        case 4: {
        }

        default:
            return -1;
    }
}

void SelectFeature(int value)
{
    switch (value) {
#if OUTER_FEATURE
#if INNER_FEATURE
        case 4:
        case 0:
        {
            Handle();
        } // The block is the entire case body.
#else
        case 1: {
                HandleOther();
            }

            Finish();
            break;
#endif
#elif FALLBACK_FEATURE
        case 2:
        {
            Fallback();
        }
#else
        default:
        {
            Reset();
        }
#endif
        case 3: {
            Done();
        }
    }
}

void SelectInLambda()
{
    Dispatch([] (int value) {
        switch (value) {
            case 0:
            case 1: {
                Handle();
                break;
            }

            case 2:
            case 3: {
            }

            case 4:
            default: {
                Reset();
            }
        }
    });
}
