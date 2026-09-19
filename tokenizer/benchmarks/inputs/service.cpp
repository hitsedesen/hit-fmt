#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace example::catalog {

using Clock = std::chrono::steady_clock;
using ItemId = std::uint64_t;

enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Information,
    Warning,
    Error,
};

enum class ItemState
{
    Pending,
    Active,
    Archived,
};

struct SourceLocation
{
    std::filesystem::path File;
    std::uint32_t Line = 0;
    std::uint32_t Column = 0;
};

struct ParseIssue
{
    LogLevel Level = LogLevel::Error;
    std::string Message;
    SourceLocation Location;
};

struct Item
{
    ItemId Id = 0;
    std::string Name;
    std::vector<std::string> Tags;
    ItemState State = ItemState::Pending;
    std::chrono::system_clock::time_point UpdatedAt;

    [[nodiscard]] bool HasTag(std::string_view tag) const
    {
        return std::ranges::find(Tags, tag) != Tags.end();
    }
};

struct AddItem
{
    Item Value;
};

struct RemoveItem
{
    ItemId Id = 0;
};

struct RenameItem
{
    ItemId Id = 0;
    std::string Name;
};

using Change = std::variant<AddItem, RemoveItem, RenameItem>;

class Logger
{
public:
    virtual ~Logger() = default;
    virtual void Write(LogLevel level, std::string_view message) = 0;
};

class StreamLogger final : public Logger
{
public:
    explicit StreamLogger(std::ostream& output)
        : Output_(output)
    { }

    void Write(LogLevel level, std::string_view message) override
    {
        static constexpr std::string_view Names[] = {
            "trace", "debug", "info", "warning", "error"};
        std::scoped_lock guard(Mutex_);
        Output_ << std::format(
            "[{}] {}\n", Names[std::to_underlying(level)], message);
    }

private:
    std::ostream& Output_;
    std::mutex Mutex_;
};

class Catalog
{
public:
    explicit Catalog(std::shared_ptr<Logger> logger)
        : Logger_(std::move(logger))
    { }

    [[nodiscard]] std::optional<Item> Find(ItemId id) const
    {
        std::shared_lock guard(Mutex_);
        if (auto iterator = Items_.find(id); iterator != Items_.end()) {
            return iterator->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Item> Search(std::string_view query) const
    {
        std::vector<Item> result;
        std::shared_lock guard(Mutex_);
        for (const auto& [id, item] : Items_) {
            if (item.Name.contains(query)
                || std::ranges::any_of(
                    item.Tags,
                    [&](const auto& tag) {
                        return tag.contains(query);
                    }))
            {
                result.push_back(item);
            }
        }
        std::ranges::sort(result, {}, &Item::Name);
        return result;
    }

    bool Apply(const Change& change)
    {
        return std::visit(
            [this]<class T>(const T& operation) {
                return ApplyOperation(operation);
            },
            change);
    }

    void Replace(std::vector<Item> items)
    {
        std::unordered_map<ItemId, Item> replacement;
        replacement.reserve(items.size());
        for (auto& item : items) {
            replacement.insert_or_assign(item.Id, std::move(item));
        }
        {
            std::unique_lock guard(Mutex_);
            Items_.swap(replacement);
            ++Generation_;
        }
        Logger_->Write(
            LogLevel::Information,
            std::format("loaded {} catalog entries", Items_.size()));
    }

    [[nodiscard]] std::uint64_t Generation() const noexcept
    {
        return Generation_.load(std::memory_order_relaxed);
    }

private:
    bool ApplyOperation(const AddItem& operation)
    {
        std::unique_lock guard(Mutex_);
        auto [iterator, inserted] =
            Items_.try_emplace(operation.Value.Id, operation.Value);
        if (!inserted) {
            Logger_->Write(
                LogLevel::Warning,
                std::format("item {} already exists", operation.Value.Id));
            return false;
        }
        ++Generation_;
        return true;
    }

    bool ApplyOperation(const RemoveItem& operation)
    {
        std::unique_lock guard(Mutex_);
        if (Items_.erase(operation.Id) == 0) {
            return false;
        }
        ++Generation_;
        return true;
    }

    bool ApplyOperation(const RenameItem& operation)
    {
        std::unique_lock guard(Mutex_);
        auto iterator = Items_.find(operation.Id);
        if (iterator == Items_.end()) {
            return false;
        }
        iterator->second.Name = operation.Name;
        iterator->second.UpdatedAt = std::chrono::system_clock::now();
        ++Generation_;
        return true;
    }

    std::shared_ptr<Logger> Logger_;
    mutable std::shared_mutex Mutex_;
    std::unordered_map<ItemId, Item> Items_;
    std::atomic<std::uint64_t> Generation_ = 0;
};

class LineReader
{
public:
    explicit LineReader(std::string_view input)
        : Remaining_(input)
    { }

    [[nodiscard]] std::optional<std::string_view> Next()
    {
        if (Remaining_.empty()) {
            return std::nullopt;
        }
        auto newline = Remaining_.find('\n');
        auto line = Remaining_.substr(0, newline);
        if (newline == std::string_view::npos) {
            Remaining_ = {};
        } else {
            Remaining_.remove_prefix(newline + 1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        ++Line_;
        return line;
    }

    [[nodiscard]] size_t LineNumber() const noexcept
    {
        return Line_;
    }

private:
    std::string_view Remaining_;
    size_t Line_ = 0;
};

class CatalogParser
{
public:
    explicit CatalogParser(std::filesystem::path source)
        : Source_(std::move(source))
    { }

    [[nodiscard]] std::vector<Item> Parse(std::string_view text)
    {
        std::vector<Item> result;
        LineReader reader(text);
        while (auto line = reader.Next()) {
            auto trimmed = Trim(*line);
            if (trimmed.empty() || trimmed.starts_with('#')) {
                continue;
            }
            if (auto item = ParseItem(trimmed, reader.LineNumber())) {
                result.push_back(std::move(*item));
            }
        }
        return result;
    }

    [[nodiscard]] const std::vector<ParseIssue>& Issues() const noexcept
    {
        return Issues_;
    }

private:
    static std::string_view Trim(std::string_view value)
    {
        constexpr std::string_view Whitespace = " \t\r\n";
        auto first = value.find_first_not_of(Whitespace);
        if (first == std::string_view::npos) {
            return {};
        }
        auto last = value.find_last_not_of(Whitespace);
        return value.substr(first, last - first + 1);
    }

    std::optional<Item> ParseItem(std::string_view line, size_t lineNumber)
    {
        std::array<std::string_view, 4> fields;
        for (auto& field : fields) {
            auto separator = line.find('|');
            field = Trim(line.substr(0, separator));
            if (separator == std::string_view::npos) {
                line = {};
            } else {
                line.remove_prefix(separator + 1);
            }
        }

        Item item;
        auto [end, error] = std::from_chars(
            fields[0].data(), fields[0].data() + fields[0].size(), item.Id);
        if (error != std::errc{} || end != fields[0].data() + fields[0].size())
        {
            Report(lineNumber, "invalid item identifier");
            return std::nullopt;
        }
        if (fields[1].empty()) {
            Report(lineNumber, "item name must not be empty");
            return std::nullopt;
        }
        item.Name = fields[1];
        item.State = ParseState(fields[2]).value_or(ItemState::Pending);

        while (!fields[3].empty()) {
            auto comma = fields[3].find(',');
            auto tag = Trim(fields[3].substr(0, comma));
            if (!tag.empty()) {
                item.Tags.emplace_back(tag);
            }
            if (comma == std::string_view::npos) {
                break;
            }
            fields[3].remove_prefix(comma + 1);
        }
        return item;
    }

    static std::optional<ItemState> ParseState(std::string_view value)
    {
        if (value == "pending") {
            return ItemState::Pending;
        }
        if (value == "active") {
            return ItemState::Active;
        }
        if (value == "archived") {
            return ItemState::Archived;
        }
        return std::nullopt;
    }

    void Report(size_t line, std::string message)
    {
        Issues_.push_back(
            ParseIssue{
                .Message = std::move(message),
                .Location =
                    {.File = Source_, .Line = static_cast<std::uint32_t>(line)},
            });
    }

    std::filesystem::path Source_;
    std::vector<ParseIssue> Issues_;
};

std::string LoadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(std::format("cannot open {}", path.string()));
    }
    input.seekg(0, std::ios::end);
    std::string contents(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0, std::ios::beg);
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    return contents;
}

int Run(std::span<const std::string_view> arguments, Logger& logger)
{
    if (arguments.size() != 2) {
        logger.Write(LogLevel::Error, "usage: catalog <input> <query>");
        return 2;
    }

    try {
        CatalogParser parser{std::filesystem::path(arguments[0])};
        auto catalog =
            Catalog(std::shared_ptr<Logger>(&logger, [](Logger*) { }));
        catalog.Replace(
            parser.Parse(LoadFile(std::filesystem::path(arguments[0]))));
        for (const auto& issue : parser.Issues()) {
            logger.Write(issue.Level, issue.Message);
        }
        for (const auto& item : catalog.Search(arguments[1])) {
            logger.Write(
                LogLevel::Information,
                std::format(
                    "{}: {} [{} tags]", item.Id, item.Name, item.Tags.size()));
        }
        return parser.Issues().empty() ? 0 : 1;
    } catch (const std::exception& error) {
        logger.Write(LogLevel::Error, error.what());
        return 1;
    }
}

} // namespace example::catalog
