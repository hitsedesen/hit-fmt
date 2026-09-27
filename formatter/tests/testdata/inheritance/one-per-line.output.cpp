struct Atlas : public East,
    private West,
    protected North {};
struct River : public Stream,
    public Channel,
    public Reservoir {};
struct Compact : First,
    Second,
    Third {};
struct Gapped : public virtual East,
    virtual protected West {};
struct Short : public One,
    public Two {};
struct Notes : public /* keep */    East, public   West {};
struct Deep : std::integral_constant<int, 3>,
    public End {};
struct Covered : First, /* keep */ Second {};
struct Blank : First,
    Second {};
struct BlankColon : First,
    Second {};
struct Single : First {};
struct Incomplete : First, public {};
enum class Flag : unsigned { Set };
