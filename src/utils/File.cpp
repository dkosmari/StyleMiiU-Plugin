#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>

#include "File.h"

using namespace std::literals;

namespace utils {

    namespace {

        [[noreturn]]
        void
        throw_from_error(int e,
                         const std::string& msg)
        {
            if (e == 0)
                e = EIO; // generic I/O error
            throw std::system_error{e, std::generic_category(), msg};
        }

    } // namespace

    File::File(const std::filesystem::path& filename,
               const char* mode) :
        fp{std::fopen(filename.c_str(), mode)}
    {
        if (!fp)
            throw_from_error(errno, "failed to open \""s + filename.string() + "\""s);
    }

    File::~File()
        noexcept
    {
        close();
    }

    File::operator bool()
        const noexcept
    {
        return fp;
    }

    void
    File::open(const std::filesystem::path& filename,
               const char* mode)
    {
        close();
        fp = std::fopen(filename.c_str(), mode);
        if (!fp)
            throw_from_error(errno, "failed to open \""s + filename.string() + "\""s);
    }

    void
    File::close()
        noexcept
    {
        if (fp) {
            std::fclose(fp);
            fp = nullptr;
        }
    }

    bool
    File::gets(char* dst,
               std::size_t n)
        noexcept
    {
        return std::fgets(dst, n, fp);
    }

    std::size_t
    File::read(void* dst,
               std::size_t n)
    {
        auto result = std::fread(dst, 1, n, fp);
        if (std::ferror(fp))
            throw_from_error(errno, "fread() failed");
        return result;
    }

    std::size_t
    File::write(const void* src,
                std::size_t n)
    {
        auto result = std::fwrite(src, 1, n, fp);
        if (std::ferror(fp))
            throw_from_error(errno, "fwrite() failed");
        return result;
    }

    void
    File::rewind()
        noexcept
    {
        if (fp)
            std::rewind(fp);
    }

} // namespace utils
