#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <memory>
#include <type_traits>

namespace wxv::memory
{
    inline void *allocatePreferPsram(size_t bytes)
    {
        if (bytes == 0)
        {
            return nullptr;
        }

        if (psramFound())
        {
            if (void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
            {
                return ptr;
            }
        }

        return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }

    inline void *reallocatePreferPsram(void *ptr, size_t bytes)
    {
        if (bytes == 0)
        {
            heap_caps_free(ptr);
            return nullptr;
        }

        if (ptr == nullptr)
        {
            return allocatePreferPsram(bytes);
        }

        if (psramFound())
        {
            if (void *resized = heap_caps_realloc(ptr, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
            {
                return resized;
            }
        }

        return heap_caps_realloc(ptr, bytes, MALLOC_CAP_8BIT);
    }

    inline void freeCaps(void *ptr)
    {
        if (ptr != nullptr)
        {
            heap_caps_free(ptr);
        }
    }

    template <typename T>
    class PsramAllocator
    {
    public:
        using value_type = T;

        PsramAllocator() noexcept = default;

        template <typename U>
        PsramAllocator(const PsramAllocator<U> &) noexcept {}

        [[nodiscard]] T *allocate(std::size_t n)
        {
            void *ptr = allocatePreferPsram(n * sizeof(T));
            if (ptr == nullptr)
            {
                throw std::bad_alloc();
            }
            return static_cast<T *>(ptr);
        }

        void deallocate(T *ptr, std::size_t) noexcept
        {
            freeCaps(ptr);
        }

        template <typename U>
        bool operator==(const PsramAllocator<U> &) const noexcept
        {
            return true;
        }

        template <typename U>
        bool operator!=(const PsramAllocator<U> &) const noexcept
        {
            return false;
        }
    };

    class PsramJsonAllocator : public ArduinoJson::Allocator
    {
    public:
        void *allocate(size_t size) override
        {
            return allocatePreferPsram(size);
        }

        void deallocate(void *ptr) override
        {
            freeCaps(ptr);
        }

        void *reallocate(void *ptr, size_t newSize) override
        {
            return reallocatePreferPsram(ptr, newSize);
        }
    };

    inline ArduinoJson::Allocator *psramJsonAllocator()
    {
        static PsramJsonAllocator allocator;
        return &allocator;
    }
}
