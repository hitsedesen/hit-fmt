void Transform(size_t count, size_t width, int value, int mask)
{
    auto product = static_cast<size_t>(count) * width;
    auto compactProduct = static_cast<size_t>(count) *width;
    auto pointer = (char*)&value;
    auto spacedPointer = (char*) & value;
    auto intersection = (value) & mask;
    auto compactIntersection = (value) &mask;
}
