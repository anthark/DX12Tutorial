#pragma once

#include <queue>

namespace Platform
{

enum class RingBufferResult
{
    Ok = 0,             // Space allocated
    NoRoom,             // No room for data of such size (it is needed to free some data)
    AllocTooLarge       // Retrieved allocation is too large and cannot be fit into buffer of current size
};

template <typename T, typename AllocType>
struct RingBuffer
{
    RingBuffer() : allocStart(0), allocEnd(0), allocMaxSize(0) {}

    bool Init(UINT64 _allocMaxSize)
    {
        allocMaxSize = _allocMaxSize;

        return true;
    }

    void Term()
    {
        allocStart = allocEnd = allocMaxSize = 0;
    }

    RingBufferResult Alloc(UINT64 allocSize, UINT64& allocStartOffset, AllocType& allocation, UINT align)
    {
        UINT64 alignedAllocEnd = Align(allocEnd, (UINT64)align);

        if (allocSize > allocMaxSize)
        {
            return RingBufferResult::AllocTooLarge;
        }

        if (allocStart <= allocEnd)
        {
            if (allocSize > allocMaxSize - alignedAllocEnd)
            {
                alignedAllocEnd = 0;
            }
        }
        if (allocStart > allocEnd)
        {
            if (allocSize > allocStart - alignedAllocEnd)
            {
                return RingBufferResult::NoRoom;
            }
        }

        allocStartOffset = alignedAllocEnd;

        allocation = static_cast<T*>(this)->At(alignedAllocEnd);

        allocEnd = alignedAllocEnd + allocSize;

        return RingBufferResult::Ok;
    }

    void AddPendingFence(UINT64 fenceValue)
    {
        pendingFences.push({fenceValue, allocEnd});
    }

    void FlashFenceValue(UINT64 fenceValue)
    {
        while (!pendingFences.empty() && pendingFences.front().fenceValue <= fenceValue)
        {
            allocStart = pendingFences.front().allocEnd;
            pendingFences.pop();
        }
    }

protected:
    struct PendingFence
    {
        UINT64 fenceValue;
        UINT64 allocEnd;
    };

protected:
    UINT64 allocStart;
    UINT64 allocEnd;
    UINT64 allocMaxSize;

    std::queue<PendingFence> pendingFences;
};

} // Platform
