#include <tokenizer/tokenizer.hpp>
#include <unicode/code_point.hpp>
#include <unicode/utf8.hpp>
#include <util/file.hpp>
#include <util/input.hpp>
#include <util/timer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

auto ToMillis(auto duration)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}

constexpr auto Extensions = std::to_array({".cpp", ".h", ".hpp", ".cxx"});

constexpr size_t MaxSourceFileSize = 4 * 1024 * 1024; // 4 MB.

std::pair<long long, long long> DoTokenizeFile(const std::string& filePath)
{
    auto fileContents = util::ReadEntireFile(filePath, MaxSourceFileSize);
    tokenizer::Tokenizer tokenizer(fileContents);
    int invalidTokens = 0;
    int validTokens = 0;
    while (true) {
        auto token = tokenizer.Get();
        if (token.Kind == tokenizer::TokenKind::EndOfFile) {
            break;
        }
        if (token.Kind == tokenizer::TokenKind::Invalid) {
            invalidTokens++;
        } else {
            validTokens++;
        }
    }
    if (invalidTokens) {
        std::cout << std::format("File has invalid tokens: {}\n", filePath);
    }
    return {validTokens, invalidTokens};
}

std::pair<long long, long long> TokenizeFile(const std::string& filePath)
{
    try {
        return DoTokenizeFile(filePath);
    } catch (std::exception& e) {
        std::cerr << std::format("Failed to tokenize file {}\n\t", filePath)
                  << e.what() << "\n";
        ;
        return {0, 0};
    }
}

void Run(const std::string& inputPath)
{
    long long validTokens = 0;
    long long invalidTokens = 0;
    if (std::filesystem::is_directory(inputPath)) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(inputPath))
        {
            if (std::ranges::contains(Extensions, entry.path().extension())) {
                auto [v, i] = TokenizeFile(entry.path().string());
                validTokens += v;
                invalidTokens += i;
            }
        }
    } else {
        auto [v, i] = TokenizeFile(inputPath);
        validTokens += v;
        invalidTokens += i;
    }
    std::cout << std::format(
        "Total valid tokens: {}, invalid tokens: {}\n",
        validTokens,
        invalidTokens);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path>\n";
        return -1;
    }

    int x = 0;
    int y = 0;

    auto inputPath = argv[1];
    try {
        util::Timer timer;
        Run(inputPath);
        auto end = std::chrono::steady_clock::now();
        std::cout << std::format(
            "Time elapsed: {}\n", ToMillis(timer.Elapsed()));
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << "\n";
        return -1;
    }
}
