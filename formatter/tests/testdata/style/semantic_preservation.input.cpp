namespace sample::formatting {

constexpr std::int64_t MaxPayloadSize = 131072;
static const FixtureLogger TestLogger("Example");

MAKE_ENUM(ChannelState,
    (Active)
    (Stopped)
);

struct Channel
    : public ReferenceBase
{
    virtual void Read() = 0;
};

DECLARE_SHARED_STRUCT(Channel)

class ChannelBase
{
public:
    DEFINE_READONLY_PROPERTY(std::string, Name);
};

std::optional<int> FindValue();
ChannelHandle CreateChannel();

template <class T>
    requires std::is_integral_v<T>
struct Constrained
{
    T Value;
};

template <class T>
    requires std::is_integral_v<T>
void UseConstrained(T value)
{
    Use(value);
}

void Process(const ChannelHandle& channel, int itemCount)
{
    auto optionalValue = FindValue();
    if (optionalValue) {
        channel->Read();
        Use(*optionalValue);
    }

    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        ProcessItem(itemIndex);
    }

    LOG_DEBUG("Items processed (ItemCount: %v)", itemCount);
    if (!channel) {
        RAISE_ERROR("Channel is not available (ItemCount: %v)", itemCount);
    }
}

} // namespace sample::formatting
