constexpr std::array<std::string_view, 5> RawEncodingTags{
    "A",
    "u8A",
    "uA",
    "UA",
    "LA",
};

constexpr std::array<std::string_view, 8> DecimalLiteralTags{
    "",
    "q",
    "n",
    "q16",
    "q32",
    "q64",
    "q128",
    "bq16",
};

constexpr std::array<std::string_view, 11> WholeNumberLiteralTags{
    "",
    "u",
    "ul",
    "ulx",
    "ux",
    "l",
    "lu",
    "lx",
    "lxu",
    "x",
    "xu",
};

template <class Item, class Tag> void PublishDescriptor(
    Item item,
    Tag tag,
    LongDescriptor& descriptor);

auto generic = [] <class FirstParameter, class SecondParameter> (
        FirstParameter first,
        SecondParameter second) { return first; };

auto value =
    ConvertRecord<int, long>(
        firstLongInputDescription,
        secondLongInputDescription);

types::Map<
    ExceptionallyLongFirstTemplateArgument,
    ExceptionallyLongSecondTemplateArgument,
    ExceptionallyLongThirdTemplateArgument> large;
