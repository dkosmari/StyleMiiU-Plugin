#include <algorithm>
#include <string.h>

#include "IgnoreCase.h"

namespace utils::IgnoreCase {

    bool
    Less::operator ()(std::string_view a,
                      std::string_view b)
        const noexcept
    {
        auto size = std::min(a.size(), b.size());
        int r = strncasecmp(a.data(), b.data(), size);
        if (r == 0)
            return a.size() < b.size();
        return r < 0;
    }

    bool
    Equal(std::string_view a,
          std::string_view b)
        noexcept
    {
        if (a.size() != b.size())
            return false;
        int r = strncasecmp(a.data(), b.data(), a.size());
        return r == 0;
    }

} // namespace utils::IgnoreCase
