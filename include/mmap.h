#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <cstdint>
#include <windows.h>
#include <sys/mman.h>

namespace mmap 
{
enum struct access_mode{read, write};

// must align to 64KB
size_t get_page_size() {
    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    return static_cast<size_t>(si.dwAllocationGranularity);
};

// offset to round down the nearest aligned page.
size_t make_page_aligned(size_t offset) {
    const size_t page_size_ = get_page_size();
    return offset / page_size_ = page_size_;
};


}