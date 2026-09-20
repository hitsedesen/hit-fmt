#pragma once

#include <tokenizer/token.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>

// Included by syntax_tree.hpp after the public syntax-tree types are defined.
namespace parser::detail {

inline constexpr size_t InitialMemoryResourceSize = 64 * 1024;

template<class T, class... Args>
[[nodiscard]] T* Make(std::pmr::memory_resource& memory, Args&&... args)
{
    static_assert(std::is_trivially_destructible_v<T>);
    return ::new (memory.allocate(sizeof(T), alignof(T)))
        T(std::forward<Args>(args)...);
}

template<class T>
[[nodiscard]] T* MakeDefaultInitialized(std::pmr::memory_resource& memory)
{
    static_assert(std::is_trivially_destructible_v<T>);
    return ::new (memory.allocate(sizeof(T), alignof(T))) T;
}

// Appends through linked chunks while building. Seal() creates a dense chunk
// directory so formatter-facing indexed access remains constant-time.
template<class Chunk, uint16_t Capacity>
class SegmentedStorage
{
public:
    struct Slot
    {
        Chunk* ChunkPtr;
        uint16_t Offset;
        uint32_t Index;
    };

    explicit SegmentedStorage(std::pmr::memory_resource& memory)
        : Memory_(&memory)
    { }

    SegmentedStorage(SegmentedStorage&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    SegmentedStorage(const SegmentedStorage&) = delete;
    SegmentedStorage& operator=(const SegmentedStorage&) = delete;

    void MoveFrom(SegmentedStorage&& other) noexcept
    {
        Memory_ = std::exchange(other.Memory_, nullptr);
        First_ = std::exchange(other.First_, nullptr);
        Last_ = std::exchange(other.Last_, nullptr);
        Chunks_ = std::exchange(other.Chunks_, nullptr);
        ChunkCount_ = std::exchange(other.ChunkCount_, 0);
        Size_ = std::exchange(other.Size_, 0);
    }

    [[nodiscard]] Slot Append()
    {
        assert(!Chunks_);
        if (!Last_ || Last_->Size == Capacity) {
            auto* chunk = MakeDefaultInitialized<Chunk>(*Memory_);
            if (Last_) {
                Last_->Next = chunk;
            } else {
                First_ = chunk;
            }
            Last_ = chunk;
            ++ChunkCount_;
        }
        auto offset = Last_->Size++;
        auto index = Size_++;
        return {.ChunkPtr = Last_, .Offset = offset, .Index = index};
    }

    void Seal()
    {
        assert(!Chunks_);
        if (ChunkCount_ == 0) {
            return;
        }
        Chunks_ = static_cast<Chunk**>(
            Memory_->allocate(sizeof(Chunk*) * ChunkCount_, alignof(Chunk*)));
        auto* chunk = First_;
        for (uint32_t i = 0; i < ChunkCount_; ++i) {
            Chunks_[i] = chunk;
            chunk = chunk->Next;
        }
    }

    [[nodiscard]] Chunk* ChunkAt(uint32_t index) const
    {
        assert(Chunks_ && index < ChunkCount_);
        return Chunks_[index];
    }

    [[nodiscard]] Chunk* ChunkAtDuringBuild(uint32_t index) const
    {
        assert(index < ChunkCount_);
        if (Chunks_) {
            return Chunks_[index];
        }
        auto* chunk = First_;
        while (index-- != 0) {
            chunk = chunk->Next;
        }
        return chunk;
    }

    [[nodiscard]] uint32_t Size() const
    {
        return Size_;
    }

private:
    static_assert(std::is_trivially_destructible_v<Chunk>);

    std::pmr::memory_resource* Memory_;
    Chunk* First_ = nullptr;
    Chunk* Last_ = nullptr;
    Chunk** Chunks_ = nullptr;
    uint32_t ChunkCount_ = 0;
    uint32_t Size_ = 0;
};

struct StoredToken
{
    util::SourceView Text;
    uint32_t Line;
};

static_assert(sizeof(StoredToken) == 12);

class TriviaPool
{
    static constexpr uint16_t AttachmentsPerChunk = 128;

    struct Chunk
    {
        Chunk* Next = nullptr;
        uint16_t Size = 0;
        std::array<TriviaAttachments, AttachmentsPerChunk> Items;
    };

public:
    explicit TriviaPool(std::pmr::memory_resource& memory)
        : Storage_(memory)
    { }

    TriviaPool(TriviaPool&& other) noexcept = default;

    TriviaPool(const TriviaPool&) = delete;
    TriviaPool& operator=(const TriviaPool&) = delete;

    void MoveFrom(TriviaPool&& other) noexcept
    {
        Storage_.MoveFrom(std::move(other.Storage_));
    }

    [[nodiscard]] uint32_t Add()
    {
        auto slot = Storage_.Append();
        slot.ChunkPtr->Items[slot.Offset] = {};
        return slot.Index;
    }

    void Seal()
    {
        Storage_.Seal();
    }

    [[nodiscard]] TriviaAttachments& operator[](uint32_t index)
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAtDuringBuild(index / AttachmentsPerChunk)
            ->Items[index % AttachmentsPerChunk];
    }

    [[nodiscard]] const TriviaAttachments& operator[](uint32_t index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAtDuringBuild(index / AttachmentsPerChunk)
            ->Items[index % AttachmentsPerChunk];
    }

private:
    SegmentedStorage<Chunk, AttachmentsPerChunk> Storage_;
};

class TokenPool
{
    static constexpr uint16_t TokensPerChunk = 512;

    struct Chunk
    {
        Chunk* Next = nullptr;
        uint16_t Size = 0;
        bool HasPreprocessingDirective = false;
        bool HasComment = false;
        std::array<StoredToken, TokensPerChunk> Tokens;
        std::array<tokenizer::TokenKind, TokensPerChunk> Kinds;
        std::array<PunctuationRole, TokensPerChunk> Roles;
    };

public:
    explicit TokenPool(std::pmr::memory_resource& memory)
        : Storage_(memory)
    { }

    TokenPool(TokenPool&& other) noexcept = default;

    TokenPool(const TokenPool&) = delete;
    TokenPool& operator=(const TokenPool&) = delete;

    void MoveFrom(TokenPool&& other) noexcept
    {
        Storage_.MoveFrom(std::move(other.Storage_));
    }

    void Add(tokenizer::Token token)
    {
        auto slot = Storage_.Append();
        auto* chunk = slot.ChunkPtr;
        chunk->HasPreprocessingDirective |=
            token.Kind == tokenizer::TokenKind::PreprocessingDirective;
        chunk->HasComment |= token.Kind == tokenizer::TokenKind::Comment;
        chunk->Tokens[slot.Offset] = {
            .Text = token.Text,
            .Line = token.Position.Line,
        };
        chunk->Kinds[slot.Offset] = token.Kind;
        chunk->Roles[slot.Offset] =
            token.Kind >= tokenizer::TokenKind::Operator_OpenBrace
                && token.Kind <= tokenizer::TokenKind::Operator_Comma
            ? PunctuationRole::Generic
            : PunctuationRole::None;
    }

    void Seal()
    {
        Storage_.Seal();
    }

    [[nodiscard]] const StoredToken& operator[](TokenIndex index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAt(index / TokensPerChunk)
            ->Tokens[index % TokensPerChunk];
    }

    [[nodiscard]] TokenIndex Size() const
    {
        return Storage_.Size();
    }

    [[nodiscard]] tokenizer::TokenKind Kind(TokenIndex index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAt(index / TokensPerChunk)
            ->Kinds[index % TokensPerChunk];
    }

    [[nodiscard]] PunctuationRole Role(TokenIndex index) const
    {
        assert(index < Storage_.Size());
        return Storage_.ChunkAt(index / TokensPerChunk)
            ->Roles[index % TokensPerChunk];
    }

    void SetRole(TokenIndex index, PunctuationRole role)
    {
        assert(index < Storage_.Size());
        Storage_.ChunkAt(index / TokensPerChunk)
            ->Roles[index % TokensPerChunk] = role;
    }

    [[nodiscard]] TokenIndex
    FindPreprocessingDirective(TokenIndex begin, TokenIndex end) const;
    [[nodiscard]] TokenIndex
    FindComment(TokenIndex begin, TokenIndex end) const;

private:
    SegmentedStorage<Chunk, TokensPerChunk> Storage_;
};

} // namespace parser::detail
