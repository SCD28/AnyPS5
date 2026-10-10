#include "prx/libc/include/GuestAllocations.hpp"
#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<bool> failNextAllocation{false};

void* operator new(std::size_t bytes) {
    if (failNextAllocation.exchange(false)) throw std::bad_alloc();
    if (void* allocation = std::malloc(bytes == 0 ? 1 : bytes)) return allocation;
    throw std::bad_alloc();
}

void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}

void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

static void Require(bool condition) {
    if (!condition) std::abort();
}

int main() {
    constexpr std::size_t page = 0x4000;
    std::vector<unsigned char> storage(page * 4);
    auto* base = reinterpret_cast<unsigned char*>((reinterpret_cast<std::uintptr_t>(storage.data()) + page - 1) & ~(page - 1));
    const auto address = reinterpret_cast<std::uintptr_t>(base);
    {
        GuestAllocations::Mutation mutation;
        mutation.Add(base, page * 3, true, true, false);
    }
    bool applied = false;
    bool failed = false;
    try {
        GuestAllocations::Mutation mutation;
        mutation.Protect(base + page, page, true, false, false, [&] {
            applied = true;
            failNextAllocation = true;
        });
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    const bool allocationWasNotAttempted = failNextAllocation.exchange(false);
    Require(applied && allocationWasNotAttempted && !failed);
    {
        const auto lease = GuestAllocations::GuestAllocationsAcquire_nid_postfix();
        bool found = false;
        for (const auto& range : lease) {
            if (range->address == address + page) found = !range->writable;
        }
        Require(found);
    }
    {
        GuestAllocations::Mutation mutation;
        mutation.Remove(base);
    }
}
