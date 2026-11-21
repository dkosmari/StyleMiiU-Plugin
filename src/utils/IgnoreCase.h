// -*- mode: c++ -*-

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace utils::IgnoreCase {

    namespace detail {

        template <typename T>
        inline
        std::string_view
        to_sv(const T& arg)
            noexcept
        {
            return std::string_view{arg};
        }

        inline
        std::string_view
        to_sv(const std::filesystem::path& arg)
            noexcept
        {
            return arg.native();
        }

    } // namespace detail

    struct Less{

        // Tell the standard library the comparison operator is templated.
        using is_transparent = void;

        bool
        operator ()(std::string_view a,
                    std::string_view b)
            const noexcept;

        template <typename A,
                  typename B>
        bool
        operator ()(const A& a,
                    const B& b)
            const noexcept
        {
            return operator ()(detail::to_sv(a), detail::to_sv(b));
        }

    }; // struct Less

    bool
    Equal(std::string_view a,
          std::string_view b)
        noexcept;

    template <typename A,
              typename B>
    inline
    bool
    Equal(const A& a,
          const B& b)
        noexcept
    {
        return Equal(detail::to_sv(a), detail::to_sv(b));
    }

} // namespace utils::IgnoreCase
