struct Callback
{
    void Run() const
    {
        Consume(Create(
#ifdef TRACK_LOCATION
            State_->Location,
#endif
            *this), &State::Invoke);
    }
};
inline Record::Record(
#ifdef TRACK_LOCATION
    const Point& point
#endif
)
#ifdef TRACK_LOCATION
    : Point_(point)
#endif
{ }

inline Callback::operator bool() const
{
    return static_cast<bool>(State_);
}
inline Record::Record(int value)
#if LEFT_SIDE
    : Left_(value)
#else
    : Right_{value}
#endif
{ }
inline Record::Record(int value)
    : Base(value),
#if EXTRA_MEMBER
      Extra_{value},
#endif
      Last_(value)
{ }
void Branches()
{
#if 0
    auto a = b * d;
#else
    auto a = c * d;
#endif
}
void BranchBodies(int descriptor)
{
    if (descriptor >= 0) {
        #ifndef PLATFORM_WINDOWS
        if (descriptor >= DescriptorLimit) {
            LogWarning("Descriptor exceeds limit")
                .With("Descriptor", descriptor)
                .With("Limit", DescriptorLimit);
            status = -1;
        }
        #else
        while (Ready()) {
            Poll();
        }
        #endif
#if OUTER_FEATURE
#if INNER_FEATURE
        if (Ready()) {
            Poll();
        }
#else
        Reset();
        Notify();
#endif
#endif
        Finish();
    }
}
void Arguments()
{
    Consume(first,
#if PICK_LEFT
        left + delta,
#elif PICK_MIDDLE
        middle - delta,
#else
        right / delta,
#endif
        *last);
    Consume(
#if OPTIONAL
#if INNER
        Extra(left),
#else
        Extra(right),
#endif
#endif
        &last);
    Consume(first
#ifdef OPTIONAL
        , Extra(value)
#endif
    );
    Consume(
#if PICK_LEFT
        left
#else
        right
#endif
    );
}
void Parameters(
#if PICK_LEFT
    int left,
#else
    double right,
#endif
    int common)
{ }
void PreserveSplitExpressions()
{
    auto a =
#if 0
        b
#else
        c
#endif
        * d;
    auto ambiguous =
#if OPTIONAL
        b
#endif
        *d;
    Consume(
#if OPTIONAL
        b
#endif
        *d);
    Consume(first +
#if PICK_LEFT
        left
#else
        right
#endif
    );
}
