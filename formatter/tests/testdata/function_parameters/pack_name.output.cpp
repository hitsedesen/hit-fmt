void Many(int alpha, int beta,
    int gamma, int delta);
void Short(int a, int b);
void ExistingName(int first, int second,
    int third, int fourth);
void ExistingNew(int first, int second,
    int third, int fourth);
void Mergeable(int a, int b);
void Nested(std::pair<int, int> value,
    int count, int mode);
void Definition(int first, int second,
    int third, int fourth)
{
    Use(first, second, third, fourth);
}
void Pinned(int alpha, // keep
    int beta, int gamma);
void NoArguments();
BUILD_TAG_TABLE(EWidgetTag,
    MAP_TAGS(TAG_NAME));
