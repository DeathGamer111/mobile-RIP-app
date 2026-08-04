#include "elf_symbols.h"

#include <algorithm>
#include <cstring>
#include <elf.h>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>

namespace {

template<typename T>
const T* objectAt(const std::vector<unsigned char>& bytes, std::size_t offset,
                  std::size_t count = 1)
{
    if (offset > bytes.size() || count > (bytes.size() - offset) / sizeof(T))
        return nullptr;
    return reinterpret_cast<const T*>(bytes.data() + offset);
}

} // namespace

bool ElfSymbols::load(const std::filesystem::path& path, std::string& error)
{
    m_entries.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open ELF file: " + path.string();
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    const auto* header = objectAt<Elf64_Ehdr>(bytes, 0);
    if (!header || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB) {
        error = "SDK is not a little-endian ELF64 file";
        return false;
    }

    const auto* sections = objectAt<Elf64_Shdr>(bytes, header->e_shoff,
                                                 header->e_shnum);
    if (!sections) {
        error = "ELF section table is outside the file";
        return false;
    }

    std::set<std::tuple<std::string, std::uint64_t, unsigned char>> seen;
    for (std::size_t sectionIndex = 0; sectionIndex < header->e_shnum;
         ++sectionIndex) {
        const auto& section = sections[sectionIndex];
        if (section.sh_type != SHT_SYMTAB && section.sh_type != SHT_DYNSYM)
            continue;
        if (section.sh_entsize < sizeof(Elf64_Sym) ||
            section.sh_link >= header->e_shnum)
            continue;

        const auto& stringSection = sections[section.sh_link];
        if (stringSection.sh_offset > bytes.size() ||
            stringSection.sh_size > bytes.size() - stringSection.sh_offset)
            continue;
        const char* strings = reinterpret_cast<const char*>(
            bytes.data() + stringSection.sh_offset);
        const auto* symbols = objectAt<Elf64_Sym>(
            bytes, section.sh_offset, section.sh_size / section.sh_entsize);
        if (!symbols)
            continue;

        const std::size_t count = section.sh_size / section.sh_entsize;
        for (std::size_t i = 0; i < count; ++i) {
            const auto* symbol = reinterpret_cast<const Elf64_Sym*>(
                reinterpret_cast<const unsigned char*>(symbols) +
                i * section.sh_entsize);
            const unsigned char type = ELF64_ST_TYPE(symbol->st_info);
            if (symbol->st_shndx == SHN_UNDEF || symbol->st_name == 0 ||
                (type != STT_OBJECT && type != STT_FUNC) ||
                symbol->st_name >= stringSection.sh_size)
                continue;
            const std::string name(strings + symbol->st_name);
            if (name.empty())
                continue;
            const auto key = std::make_tuple(name, symbol->st_value, type);
            if (!seen.insert(key).second)
                continue;
            m_entries.push_back({name, symbol->st_value, symbol->st_size, type,
                                 ELF64_ST_BIND(symbol->st_info),
                                 section.sh_type == SHT_DYNSYM});
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const auto& left,
                                                      const auto& right) {
        if (left.name != right.name)
            return left.name < right.name;
        return left.value < right.value;
    });
    return true;
}

const ElfSymbol* ElfSymbols::find(const std::string& name) const
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [&](const auto& item) {
                                     return item.name == name;
                                 });
    if (it != m_entries.end())
        return &*it;

    // File-local C++ globals keep their source name inside an _ZL... mangled
    // symbol (for example _ZL9pCurSwath). They are intentionally absent from
    // dlsym, but their .symtab value is still usable relative to the link map.
    const auto local = std::find_if(m_entries.begin(), m_entries.end(),
                                    [&](const auto& item) {
                                        return item.type == STT_OBJECT &&
                                               item.name.rfind("_ZL", 0) == 0 &&
                                               item.name.size() >= name.size() &&
                                               item.name.compare(item.name.size() - name.size(),
                                                                 name.size(), name) == 0;
                                    });
    return local == m_entries.end() ? nullptr : &*local;
}
