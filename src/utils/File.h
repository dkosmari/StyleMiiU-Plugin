// -*- mode: c++ -*-
#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <ranges>
#include <span>

namespace utils {

    // RAII type for FILE pointers
    struct File {

        std::FILE* fp = nullptr;

        File() = default;

        File(const std::filesystem::path& filename,
             const char* mode);

        ~File()
            noexcept;

        operator bool()
            const noexcept;

        void
        open(const std::filesystem::path& filename,
             const char* mode);

        void
        close()
            noexcept;

        bool
        gets(char* dst,
             std::size_t n)
            noexcept;

        template <std::ranges::contiguous_range R>
        bool
        gets(R&& dst)
            noexcept
        {
            std::span s{dst};
            return gets(s.data(), s.size_bytes());
        }

        std::size_t
        read(void* dst,
             std::size_t n);

        template <std::ranges::contiguous_range R>
        std::size_t
        read(R&& dst)
        {
            std::span s{dst};
            return read(s.data(), s.size_bytes());
        }

        std::size_t
        write(const void* src,
              std::size_t n);

        template <std::ranges::contiguous_range R>
        std::size_t
        write(R&& src)
        {
            std::span s{src};
            return write(s.data(), s.size_bytes());
        }

        void
        rewind()
            noexcept;

    };

} // namespace utils
