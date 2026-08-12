#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ElfSymbol
{
    std::string name;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    unsigned char type = 0;
    unsigned char binding = 0;
    bool dynamic = false;
};
class ElfSymbols
{
public:
    bool load(const std::filesystem::path& path, std::string& error);
    const std::vector<ElfSymbol>& entries() const { return m_entries; }
    const std::string& buildId() const { return m_buildId; }
    const ElfSymbol* find(const std::string& name) const;

private:
    std::vector<ElfSymbol> m_entries;
    std::string m_buildId;
};
