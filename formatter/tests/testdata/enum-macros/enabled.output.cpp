DEFINE_ENUM(EPhase,
    ((Waiting) (0))
    ((Working) ( 1 + 2 ))
    ((Done)    (Compute(3, 4)))
);

DEFINE_ENUM(EImplicit,
    (First)
    (Second)
    (Last)
);

DEFINE_BIT_ENUM(EBits,
    ((Read)        (1 << 0))
    ((WriteAccess) (1 << 1)) // writable
    // reserved flags
    ((Execute)     (1 << 2))
);

DEFINE_ENUM_WITH_UNDERLYING_TYPE(ECodes, unsigned long,
    ((Short)      (-1))
    ((LongerName) (0xff))
);

struct TOptions
{
    DECLARE_CHOICES(EMode,
        ((Auto)   (0))
        ((Manual) (1))
    );
};

void Run()
{
    DEFINE_ENUM(ELocal,
        (One)
        ((Two) (2))
    );
}

UNCONFIGURED(EKeep, ((A)(0)) ((Long)(1)));
DEFINE_ENUM_EXTRA(EKeep, ((A)(0)) ((Long)(1)));
DEFINE_ENUM(EMalformed, ((A)(0)), ((B)(1)));
DEFINE_ENUM(EUnsupported, ((A)(0)("a")));
DEFINE_ENUM(EEmpty, );

// hit-fmt off
DEFINE_ENUM(EFrozen, ((Short)(0)) ((LongName)(1)));
// hit-fmt on
