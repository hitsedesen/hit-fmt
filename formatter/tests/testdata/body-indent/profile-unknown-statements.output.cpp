static inline uint64_t Compute(uint64_t value)
{
    uint64_t rem = value;
    for (int i = 0; i < 8; ++i) {
        rem ^= i;
    }
    return rem;
}

void Nested()
{
    Prepare();
    if (Ready()) {
        External value = MakeValue();
        for (int i = 0; i < 2; ++i) {
            Use(value, i);
        }
    }
    Finish();
}

struct Worker
{
    Result Run()
    {
        external::Value value;
        External * pointer;
        External& reference = value;
        Use(pointer, reference);
        return value;
    }
};

void Inline()
{
    External value;
    Use(value);
}

void Opaque()
{
    unknown  *  value;
    External item =
                MakeValue(  first,second  );
    Finish();
}

// hit-fmt off
void Disabled()
    {
        External value;
        Use(value);
    }
// hit-fmt on

void AfterDisabled()
{
    External other;
    Use(other);
}

void Incomplete()
{
        External value
}
