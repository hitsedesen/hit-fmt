struct TFrameHead {};
struct TFrameTail {};

constexpr int Headroom = sizeof(TFrameHead) - sizeof(TFrameTail);
constexpr int Footroom = alignof(TFrameHead) - alignof(TFrameTail);
constexpr int Slack = sizeof(TFrameHead)-sizeof(TFrameTail) + 3;
constexpr int Negative = sizeof(TFrameHead) - - 2;

unsigned Mask(unsigned shift)
{
    using TMask = unsigned long;
    auto upper = (TMask(1) << shift) - 1;
    return (TMask(2) + shift)-1;
}
