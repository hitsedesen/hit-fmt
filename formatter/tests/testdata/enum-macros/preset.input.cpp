DEFINE_ENUM(EPhase,((Waiting)(0))((Running)(1))); // phases

DEFINE_BIT_ENUM_WITH_UNDERLYING_TYPE(EFlags, ui64, ((Read)(1ULL << 0))((WriteAccess)(1ULL << 1)));

DEFINE_AMBIGUOUS_ENUM_WITH_UNDERLYING_TYPE(EAlias, int, ((First)(0))((Second)(0)));

YT_DEFINE_ERROR_ENUM(((Broken)(42)));

void Process() { if (Ready()) Finish(); }
