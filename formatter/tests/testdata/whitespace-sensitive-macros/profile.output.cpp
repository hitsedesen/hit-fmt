DEFINE_ENUM(EChannelSide,
    (Client)
    (Server));

DEFINE_ENUM(EReady,
    (Pending)
    (Active)
);

DEFINE_ENUM(EOne, (Single));

DEFINE_BIT_ENUM(EBitFlags,
    ((Read) (1))
    ((Write) (2))
);

DEFINE_BIT_ENUM_WITH_UNDERLYING_TYPE(EBitKinds, unsigned,
    (First)
    (Second));

DEFINE_ENUM_WITH_UNDERLYING_TYPE(EPhases, int,
    (Early)
    (Late)
);

DEFINE_AMBIGUOUS_ENUM_WITH_UNDERLYING_TYPE(EPhantomStage, unsigned short,
    ((Queued) (3))
    ((Running) (7))
);

YT_DEFINE_ERROR_ENUM(
    ((Missing) (10))
    ((Invalid) (20))
);

DEFINE_ENUM_HELPER(EOther,
    (First)
    (Second));

void Process(
    int first,
    int second);

void Read()
{
    auto spelling = PP_STRINGIZE( first+  second );
    Use(spelling);
}
