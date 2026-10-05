int SelectPlatform(int value)
{
    switch (value) {
#ifdef PLATFORM_SUPPORTED
        case 0: {
            return 0;
        }
#endif
        case 1: {
            return 1;
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
