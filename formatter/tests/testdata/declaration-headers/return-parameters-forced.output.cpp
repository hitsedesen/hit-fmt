[[nodiscard]] Result<
    std::pmr::string> ReadFile(const Char* path, size_t maxSize, size_t padding, std::pmr::memory_resource& memory);
[[nodiscard]] Result<
    std::pmr::string> ReadNested(
    const Char* path,
    size_t maxSize,
    size_t padding,
    std::pmr::vector<
        std::pmr::string>& items);
[[nodiscard]] Result<
    std::pmr::vector<
        std::pmr::string>> ReadMany(
    const Char* path,
    size_t maxSize,
    size_t padding,
    std::pmr::memory_resource& memory) {
    return {};
}
[[nodiscard]] Result<
    Container<
        FirstDescriptiveResultElement,
        SecondDescriptiveResultElement,
        ThirdDescriptiveResultElement,
        FourthDescriptiveResultElement>> ReadLarge(
    const Char* path,
    size_t maxSize,
    size_t padding,
    std::pmr::memory_resource& memory);
