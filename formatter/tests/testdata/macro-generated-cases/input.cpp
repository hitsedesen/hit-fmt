const char* Describe(int group, int value)
{
#define ENTRY(tag, text) case tag: return text;
    switch (group) {
        case 1: switch (value) {
            ENTRY( 11, "first")
            ENTRY( 12, "second")
            // Generated entries are not C++ call statements.
            ENTRY( 13, "third")
            default: return nullptr;
        }
        case 2: switch (value) {
            ENTRY( 21, "one")
#if EXTRA_ENTRY
            ENTRY( 22, "two")
#endif
            default: return nullptr;
        }
        default: return nullptr;
    }
#undef ENTRY
}

void Dispatch(int value)
{
    switch (value) {
        case 0: Consume( 0 ); break;
#define SELECT_CASE(tag, type) case tag: Consume<type>(); break;
        SELECT_CASE( 1, Byte)
        SELECT_CASE( 2, Byte)
        SELECT_CASE( 3, Word)
#undef SELECT_CASE
        default: Consume( 4 ); break;
    }
}

void OpaqueTail(int value)
{
    switch (value) {
        SELECT_CASE( 5, Byte)
        SELECT_CASE( 6, Word)
    }
}

void ExplicitBoundary(int value)
{
    switch (value) {
        SELECT_CASE( 7, Byte)
        case 8: Consume( 8 ); break;
        TOKENS(case 9: break;)
        default: Consume( 10 ); break;
    }
}

Handler* BlocksBeforeMacros(int value)
{
#define LOCAL_ENTRY(tag) case tag: Consume(tag); break;
    switch (value) {
        case 0: {
            Consume(0);
            break;
        }
        case 1: {
            static Handler handler(true);
            return &handler;
        }
        LOCAL_ENTRY( 2 )
        LOCAL_ENTRY( 3 )
        case 4:
        {
            Consume(4);
            break;
        } // The opaque run can introduce new labels.
        LOCAL_ENTRY( 5 )
        case 6: {
            Consume(6);
        }
        Finish();
        break;
        case 7:
            Prepare();
            {
                Consume(7);
            }
        LOCAL_ENTRY( 8 )
        default: {
            Consume(-1);
            break;
        }
    }
    return nullptr;
#undef LOCAL_ENTRY
}

void BlockBeforeOpaqueTail(int value)
{
    switch (value) {
        case 0: {
            Consume(0);
            break;
        }
        SELECT_CASE( 1, Byte)
    }
}

void OrdinaryCalls()
{
    Consume( 1 );
    Consume(
        2,
        3);
    Factory( 4 )
        ( 5 );
}
