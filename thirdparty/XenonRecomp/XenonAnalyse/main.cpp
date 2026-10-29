#include <cassert>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <string_view>
#include <unordered_set>
#include <file.h>
#include <disasm.h>
#include <image.h>
#include <xbox.h>
#include <fmt/core.h>
#include "function.h"

#define SWITCH_ABSOLUTE 0
#define SWITCH_COMPUTED 1
#define SWITCH_BYTEOFFSET 2
#define SWITCH_SHORTOFFSET 3

struct SwitchTable
{
    std::vector<size_t> labels{};
    size_t base{};
    size_t defaultLabel{};
    uint32_t r{};
    uint32_t type{};
};

void ReadTable(Image& image, SwitchTable& table)
{
    uint32_t pOffset;
    ppc_insn insn;
    auto* code = (uint32_t*)image.Find(table.base);
    ppc::Disassemble(code, table.base, insn);
    pOffset = insn.operands[1] << 16;

    ppc::Disassemble(code + 1, table.base + 4, insn);
    pOffset += insn.operands[2];

    if (table.type == SWITCH_ABSOLUTE)
    {
        const auto* offsets = (be<uint32_t>*)image.Find(pOffset);
        for (size_t i = 0; i < table.labels.size(); i++)
        {
            table.labels[i] = offsets[i];
        }
    }
    else if (table.type == SWITCH_COMPUTED)
    {
        uint32_t base;
        uint32_t shift;
        const auto* offsets = (uint8_t*)image.Find(pOffset);

        ppc::Disassemble(code + 4, table.base + 0x10, insn);
        base = insn.operands[1] << 16;

        ppc::Disassemble(code + 5, table.base + 0x14, insn);
        base += insn.operands[2];

        ppc::Disassemble(code + 3, table.base + 0x0C, insn);
        shift = insn.operands[2];

        for (size_t i = 0; i < table.labels.size(); i++)
        {
            table.labels[i] = base + (offsets[i] << shift);
        }
    }
    else if (table.type == SWITCH_BYTEOFFSET || table.type == SWITCH_SHORTOFFSET)
    {
        if (table.type == SWITCH_BYTEOFFSET)
        {
            const auto* offsets = (uint8_t*)image.Find(pOffset);
            uint32_t base;

            ppc::Disassemble(code + 3, table.base + 0x0C, insn);
            base = insn.operands[1] << 16;

            ppc::Disassemble(code + 4, table.base + 0x10, insn);
            base += insn.operands[2];

            for (size_t i = 0; i < table.labels.size(); i++)
            {
                table.labels[i] = base + offsets[i];
            }
        }
        else if (table.type == SWITCH_SHORTOFFSET)
        {
            const auto* offsets = (be<uint16_t>*)image.Find(pOffset);
            uint32_t base;

            ppc::Disassemble(code + 4, table.base + 0x10, insn);
            base = insn.operands[1] << 16;

            ppc::Disassemble(code + 5, table.base + 0x14, insn);
            base += insn.operands[2];

            for (size_t i = 0; i < table.labels.size(); i++)
            {
                table.labels[i] = base + offsets[i];
            }
        }
    }
    else
    {
        assert(false);
    }
}

void ScanTable(const uint32_t* code, size_t base, SwitchTable& table)
{
    ppc_insn insn;
    uint32_t cr{ (uint32_t)-1 };
    for (int i = 0; i < 32; i++)
    {
        ppc::Disassemble(&code[-i], base - (4 * i), insn);
        if (insn.opcode == nullptr)
        {
            continue;
        }

        if (cr == -1 && (insn.opcode->id == PPC_INST_BGT || insn.opcode->id == PPC_INST_BGTLR || insn.opcode->id == PPC_INST_BLE || insn.opcode->id == PPC_INST_BLELR))
        {
            cr = insn.operands[0];
            if (insn.opcode->operands[1] != 0)
            {
                table.defaultLabel = insn.operands[1];
            }
        }
        else if (cr != -1)
        {
            if (insn.opcode->id == PPC_INST_CMPLWI && insn.operands[0] == cr)
            {
                table.r = insn.operands[1];
                table.labels.resize(insn.operands[2] + 1);
                table.base = base;
                break;
            }
        }
    }
}

void MakeMask(const uint32_t* instructions, size_t count)
{
    ppc_insn insn;
    for (size_t i = 0; i < count; i++)
    {
        ppc::Disassemble(&instructions[i], 0, insn);
        fmt::println("0x{:X}, // {}", ByteSwap(insn.opcode->opcode | (insn.instruction & insn.opcode->mask)), insn.opcode->name);
    }
}

void* SearchMask(const void* source, const uint32_t* compare, size_t compareCount, size_t size)
{
    assert(size % 4 == 0);
    uint32_t* src = (uint32_t*)source;
    size_t count = size / 4;
    ppc_insn insn;

    for (size_t i = 0; i < count; i++)
    {
        size_t c = 0;
        for (c = 0; c < compareCount; c++)
        {
            ppc::Disassemble(&src[i + c], 0, insn);
            if (insn.opcode == nullptr || insn.opcode->id != compare[c])
            {
                break;
            }
        }

        if (c == compareCount)
        {
            return &src[i];
        }
    }

    return nullptr;
}

// Jump tables the fixed instruction patterns below miss, because the compiler
// ordered the instructions differently (FM4: lis; rlwinm; addi; lwzx; mtctr;
// bctr). Works back from a bctr through the registers that feed it, within the
// straight-line code before it.
struct TracedInsn
{
    ppc_insn insn{};
    size_t address{};
};

static bool EndsBlock(const ppc_insn& insn)
{
    switch (insn.opcode->id)
    {
    case PPC_INST_B:
    case PPC_INST_BLR:
    case PPC_INST_BCTR:
        return true;
    default:
        return false;
    }
}

static bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

static bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// Whether the instruction writes general-purpose register `reg`. Loads and
// stores with update also write their base register; floating-point and vector
// instructions number their own registers, and stores, compares, moves to
// special registers and branches only read theirs.
static bool WritesGpr(const ppc_insn& insn, uint32_t reg)
{
    const std::string_view name = insn.opcode->name;
    const bool update = (StartsWith(name, "l") || StartsWith(name, "st")) && (EndsWith(name, "u") || EndsWith(name, "ux"));
    if (update && insn.operands[1] == reg)
        return true;
    for (const std::string_view reader : { "st", "cmp", "mt", "b", "dcb", "icb", "tw", "td", "sync", "isync", "eieio", "nop",
        "f", "lf", "v", "lv", "mffs", "mfvscr", "cr", "mcrf" })
    {
        if (StartsWith(name, reader))
            return false;
    }
    return insn.operands[0] == reg;
}

static bool IsCall(const ppc_insn& insn)
{
    switch (insn.opcode->id)
    {
    case PPC_INST_BL:
    case PPC_INST_BCTRL:
    case PPC_INST_BLRL:
        return true;
    default:
        return false;
    }
}

// The nearest instruction before `address` (at most `window` back, within the
// straight-line code) that writes `reg`.
static bool FindWriter(const Image& image, size_t address, uint32_t reg, TracedInsn& out, int window = 16)
{
    for (int i = 1; i <= window; i++)
    {
        const size_t a = address - 4 * i;
        const auto* code = (const uint32_t*)image.Find(a);
        if (code == nullptr)
            return false;
        ppc_insn insn;
        ppc::Disassemble(code, a, insn);
        if (insn.opcode == nullptr || EndsBlock(insn) || IsCall(insn))
            return false;
        if (WritesGpr(insn, reg))
        {
            out = { insn, a };
            return true;
        }
    }
    return false;
}

// The constant a lis/addi/ori chain leaves in `reg` at `address`.
static bool ConstantOf(const Image& image, size_t address, uint32_t reg, uint32_t& value, size_t& first, int depth = 0)
{
    TracedInsn w;
    if (depth > 4 || !FindWriter(image, address, reg, w))
        return false;
    first = (std::min)(first, w.address);
    switch (w.insn.opcode->id)
    {
    case PPC_INST_LIS:
        value = w.insn.operands[1] << 16;
        return true;
    case PPC_INST_LI:
        value = w.insn.operands[1];
        return true;
    case PPC_INST_ADDI:
        if (!ConstantOf(image, w.address, w.insn.operands[1], value, first, depth + 1))
            return false;
        value += w.insn.operands[2];
        return true;
    case PPC_INST_ORI:
        if (!ConstantOf(image, w.address, w.insn.operands[1], value, first, depth + 1))
            return false;
        value |= w.insn.operands[2];
        return true;
    default:
        return false;
    }
}

// `reg` = index << shift at `address`: the index register and the shift.
static bool ScaledIndex(const Image& image, size_t address, uint32_t reg, uint32_t shift, uint32_t& index, size_t& first)
{
    if (shift == 0)
    {
        index = reg;
        return true;
    }
    TracedInsn w;
    if (!FindWriter(image, address, reg, w) || w.insn.opcode->id != PPC_INST_RLWINM)
        return false;
    // rlwinm rA, rS, sh, 0, 31 - sh is slwi rA, rS, sh.
    if (w.insn.operands[2] != shift || w.insn.operands[3] != 0 || w.insn.operands[4] != 31 - shift)
        return false;
    first = (std::min)(first, w.address);
    index = w.insn.operands[1];
    return true;
}

// A table load `op rD, rA, rB`: the table address and the index register.
static bool TableLoad(const Image& image, const TracedInsn& load, uint32_t shift, uint32_t& table, uint32_t& index, size_t& first)
{
    for (int swap = 0; swap < 2; swap++)
    {
        const uint32_t base = load.insn.operands[1 + swap];
        const uint32_t offset = load.insn.operands[2 - swap];
        size_t f = first;
        if (base != 0 && ConstantOf(image, load.address, base, table, f) && ScaledIndex(image, load.address, offset, shift, index, f))
        {
            first = (std::min)(f, load.address);
            return true;
        }
    }
    return false;
}

// The number of entries a table indexed by `index` has, seen from `first`, the
// table code's first instruction. Uses the smaller of:
//  - a mask on the index (clrlwi rI, rS, n: 2^(32-n) entries);
//  - cmplwi crN, rX, count - 1 with a bgt/ble on crN, up to 160 instructions back
//    in address order, where rX holds the index's value: rI itself, a register
//    the index was copied from (mr rI, rX), or, in unoptimised code, a register
//    loaded from the stack slot the index was loaded from (lwz rI, d(r1)).
//    Scanning stops where the value is overwritten, at a blr, and at a call
//    while the value is in a volatile register or on the stack.
// FM4 often masks a byte index and then compares it, so the mask alone would
// read past the table. Without either, an earlier table on the same index that
// has `first` as a case gives the bound: a second dispatch reached only from
// that table shares it.
static bool FindBound(const Image& image, size_t sectionBase, size_t first, uint32_t index, const std::vector<SwitchTable>& known,
    size_t& count, uint32_t& defaultLabel)
{
    TracedInsn w;
    size_t maskCount = 0;
    if (FindWriter(image, first, index, w) && w.insn.opcode->id == PPC_INST_CLRLWI && w.insn.operands[2] >= 24)
        maskCount = size_t(1) << (32 - w.insn.operands[2]);

    struct Compare
    {
        size_t count;
        uint32_t defaultLabel;
    };
    // The target of the bgt/ble on each condition register: the default case.
    std::unordered_map<uint32_t, uint32_t> branchOnCr;
    // Bounded compares seen so far, by the register they compare. A compare
    // counts once the scan reaches where its register got the index's value.
    std::unordered_map<uint32_t, Compare> compares;
    auto accept = [&](const Compare& c)
        {
            count = maskCount != 0 ? (std::min)(c.count, maskCount) : c.count;
            defaultLabel = c.defaultLabel;
            return true;
        };

    // Where the index's value lives at this point of the scan: a register, or
    // (once the register's writer has been passed) a stack slot.
    uint32_t reg = index;
    bool inSlot = false;
    uint32_t slot = 0;
    for (int i = 1; i <= 160 && first - 4 * i >= sectionBase; i++)
    {
        const size_t a = first - 4 * i;
        const auto* code = (const uint32_t*)image.Find(a);
        if (code == nullptr)
            break;
        ppc_insn insn;
        ppc::Disassemble(code, a, insn);
        if (insn.opcode == nullptr)
            continue; // padding or an inline table
        const uint32_t id = insn.opcode->id;
        if (id == PPC_INST_BGT || id == PPC_INST_BLE)
        {
            branchOnCr[insn.operands[0]] = insn.operands[1];
            continue;
        }
        if (id == PPC_INST_BGTLR || id == PPC_INST_BLELR)
        {
            branchOnCr[insn.operands[0]] = 0;
            continue;
        }
        if (id == PPC_INST_CMPLWI)
        {
            const auto cr = branchOnCr.find(insn.operands[0]);
            if (cr != branchOnCr.end())
            {
                const Compare c{ size_t(insn.operands[2]) + 1, cr->second };
                if (!inSlot && insn.operands[1] == reg)
                    return accept(c);
                compares[insn.operands[1]] = c;
            }
            continue;
        }
        if (id == PPC_INST_BLR)
            break;

        if (inSlot)
        {
            // A load of the slot into a compared register.
            if (id == PPC_INST_LWZ && insn.operands[2] == 1 && insn.operands[1] == slot && compares.count(insn.operands[0]))
                return accept(compares[insn.operands[0]]);
            const bool storesToStack = StartsWith(insn.opcode->name, "st") && !EndsWith(insn.opcode->name, "x") && insn.operands[2] == 1;
            if (IsCall(insn) || WritesGpr(insn, 1) || (storesToStack && int32_t(insn.operands[1]) < int32_t(slot) + 4 &&
                int32_t(insn.operands[1]) + 8 > int32_t(slot)))
                break;
        }
        else if (WritesGpr(insn, reg))
        {
            if (id == PPC_INST_MR)
            {
                // The value came from another register.
                reg = insn.operands[1];
                if (compares.count(reg))
                    return accept(compares[reg]);
            }
            else if (id == PPC_INST_LWZ && insn.operands[2] == 1)
            {
                inSlot = true;
                slot = insn.operands[1];
            }
            else
            {
                break;
            }
        }
        else if (IsCall(insn) && (reg == 0 || (reg >= 3 && reg <= 12)))
        {
            break;
        }

        // A compared register overwritten here held another value when it was compared.
        for (auto it = compares.begin(); it != compares.end();)
            it = WritesGpr(insn, it->first) ? compares.erase(it) : std::next(it);
    }

    if (maskCount != 0)
    {
        count = maskCount;
        return true;
    }

    for (const auto& table : known)
    {
        if (table.r == index && std::find(table.labels.begin(), table.labels.end(), first) != table.labels.end())
        {
            count = table.labels.size();
            return true;
        }
    }
    return false;
}

enum class TraceResult
{
    NotATable,
    Unresolved,
    Found,
};

static TraceResult TraceBctr(const Image& image, size_t sectionBase, size_t bctr, const std::vector<SwitchTable>& known, SwitchTable& table)
{
    TracedInsn mtctr{};
    bool foundMtctr = false;
    for (int i = 1; i <= 4 && !foundMtctr; i++)
    {
        const auto* code = (const uint32_t*)image.Find(bctr - 4 * i);
        ppc::Disassemble(code, bctr - 4 * i, mtctr.insn);
        if (mtctr.insn.opcode == nullptr || EndsBlock(mtctr.insn))
            return TraceResult::NotATable;
        mtctr.address = bctr - 4 * i;
        foundMtctr = mtctr.insn.opcode->id == PPC_INST_MTCTR;
    }
    if (!foundMtctr)
        return TraceResult::NotATable;

    TracedInsn target;
    if (!FindWriter(image, mtctr.address, mtctr.insn.operands[0], target))
        return TraceResult::NotATable;

    size_t first = target.address;
    uint32_t tableAddress = 0;
    uint32_t index = 0;
    uint32_t labelBase = 0;
    uint32_t shift = 0;
    if (target.insn.opcode->id == PPC_INST_LWZX)
    {
        table.type = SWITCH_ABSOLUTE;
        if (!TableLoad(image, target, 2, tableAddress, index, first))
        {
            // A word-indexed load from a constant table that fails is worth a
            // warning; a load with another stride (a table of structs holding
            // function pointers) is an indirect call.
            uint32_t ignored = 0;
            size_t f = first;
            const bool wordIndexed = ScaledIndex(image, target.address, target.insn.operands[1], 2, ignored, f) ||
                ScaledIndex(image, target.address, target.insn.operands[2], 2, ignored, f);
            return wordIndexed ? TraceResult::Unresolved : TraceResult::NotATable;
        }
    }
    else if (target.insn.opcode->id == PPC_INST_ADD)
    {
        // base + offset[index] (byte or halfword offsets, optionally shifted).
        bool resolved = false;
        for (int swap = 0; swap < 2 && !resolved; swap++)
        {
            size_t f = first;
            if (!ConstantOf(image, target.address, target.insn.operands[1 + swap], labelBase, f))
                continue;
            TracedInsn offset;
            if (!FindWriter(image, target.address, target.insn.operands[2 - swap], offset))
                continue;
            shift = 0;
            if (offset.insn.opcode->id == PPC_INST_RLWINM && offset.insn.operands[3] == 0 &&
                offset.insn.operands[4] == 31 - offset.insn.operands[2])
            {
                shift = offset.insn.operands[2];
                f = (std::min)(f, offset.address);
                if (!FindWriter(image, offset.address, offset.insn.operands[1], offset))
                    continue;
            }
            if (offset.insn.opcode->id == PPC_INST_LBZX)
            {
                table.type = shift != 0 ? SWITCH_COMPUTED : SWITCH_BYTEOFFSET;
                resolved = TableLoad(image, offset, 0, tableAddress, index, f);
            }
            else if (offset.insn.opcode->id == PPC_INST_LHZX && shift == 0)
            {
                table.type = SWITCH_SHORTOFFSET;
                resolved = TableLoad(image, offset, 1, tableAddress, index, f);
            }
            if (resolved)
                first = f;
        }
        // An add that does not read a table is some other computed jump.
        if (!resolved)
            return TraceResult::NotATable;
    }
    else
    {
        return TraceResult::NotATable;
    }

    size_t count = 0;
    uint32_t defaultLabel = 0;
    if (!FindBound(image, sectionBase, first, index, known, count, defaultLabel))
    {
        // An absolute table right after the bctr, with no bound in the code,
        // ends where its first case begins.
        if (table.type != SWITCH_ABSOLUTE || tableAddress != bctr + 4)
            return TraceResult::Unresolved;
        const auto* words = (const be<uint32_t>*)image.Find(tableAddress);
        uint32_t end = 0xFFFFFFFF;
        for (count = 0; tableAddress + 4 * count < end && count < 1024; count++)
        {
            const uint32_t label = words[count];
            if (label <= tableAddress + 4 * count)
                return TraceResult::Unresolved;
            end = (std::min)(end, label);
        }
        if (tableAddress + 4 * count != end)
            return TraceResult::Unresolved;
    }

    table.labels.resize(count);
    for (size_t i = 0; i < table.labels.size(); i++)
    {
        switch (table.type)
        {
        case SWITCH_ABSOLUTE:
            table.labels[i] = ((const be<uint32_t>*)image.Find(tableAddress))[i];
            break;
        case SWITCH_COMPUTED:
        case SWITCH_BYTEOFFSET:
            table.labels[i] = labelBase + (uint32_t(((const uint8_t*)image.Find(tableAddress))[i]) << shift);
            break;
        case SWITCH_SHORTOFFSET:
            table.labels[i] = labelBase + ((const be<uint16_t>*)image.Find(tableAddress))[i];
            break;
        }
        // Every case must be code near the jump.
        const size_t label = table.labels[i];
        if (label % 4 != 0 || label + 0x40000 < bctr || label > bctr + 0x40000)
            return TraceResult::Unresolved;
    }
    table.base = first;
    table.r = index;
    table.defaultLabel = defaultLabel;
    return TraceResult::Found;
}

static std::string out;

template<class... Args>
static void println(fmt::format_string<Args...> fmt, Args&&... args)
{
    fmt::vformat_to(std::back_inserter(out), fmt.get(), fmt::make_format_args(args...));
    out += '\n';
};

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("Usage: XenonAnalyse [input XEX file path] [output jump table TOML file path]");
        return EXIT_SUCCESS;
    }

    const auto file = LoadFile(argv[1]);
    auto image = Image::ParseImage(file.data(), file.size());

    auto printTable = [&](const SwitchTable& table)
        {
            println("[[switch]]");
            println("base = 0x{:X}", table.base);
            println("r = {}", table.r);
            println("default = 0x{:X}", table.defaultLabel);
            println("labels = [");
            for (const auto& label : table.labels)
            {
                println("    0x{:X},", label);
            }

            println("]");
            println("");
        };

    std::vector<SwitchTable> switches{};

    println("# Generated by XenonAnalyse");

    auto scanPattern = [&](uint32_t* pattern, size_t count, size_t type)
        {
            for (const auto& section : image.sections)
            {
                if (!(section.flags & SectionFlags_Code))
                {
                    continue;
                }

                size_t base = section.base;
                uint8_t* data = section.data;
                uint8_t* dataStart = section.data;
                uint8_t* dataEnd = section.data + section.size;
                while (data < dataEnd && data != nullptr)
                {
                    data = (uint8_t*)SearchMask(data, pattern, count, dataEnd - data);

                    if (data != nullptr)
                    {
                        SwitchTable table{};
                        table.type = type;
                        ScanTable((uint32_t*)data, base + (data - dataStart), table);

                        // fmt::println("{:X} ; jmptable - {}", base + (data - dataStart), table.labels.size());
                        if (table.base != 0)
                        {
                            ReadTable(image, table);
                            printTable(table);
                            switches.emplace_back(std::move(table));
                        }

                        data += 4;
                    }
                    continue;
                }
            }
        };

    uint32_t absoluteSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_RLWINM,
        PPC_INST_LWZX,
        PPC_INST_MTCTR,
        PPC_INST_BCTR,
    };

    uint32_t computedSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_LBZX,
        PPC_INST_RLWINM,
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_ADD,
        PPC_INST_MTCTR,
    };

    uint32_t offsetSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_LBZX,
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_ADD,
        PPC_INST_MTCTR,
    };

    uint32_t wordOffsetSwitch[] =
    {
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_RLWINM,
        PPC_INST_LHZX,
        PPC_INST_LIS,
        PPC_INST_ADDI,
        PPC_INST_ADD,
        PPC_INST_MTCTR,
    };

    println("# ---- ABSOLUTE JUMPTABLE ----");
    scanPattern(absoluteSwitch, std::size(absoluteSwitch), SWITCH_ABSOLUTE);

    println("# ---- COMPUTED JUMPTABLE ----");
    scanPattern(computedSwitch, std::size(computedSwitch), SWITCH_COMPUTED);

    println("# ---- OFFSETED JUMPTABLE ----");
    scanPattern(offsetSwitch, std::size(offsetSwitch), SWITCH_BYTEOFFSET);
    scanPattern(wordOffsetSwitch, std::size(wordOffsetSwitch), SWITCH_SHORTOFFSET);

    // Every bctr the patterns above did not cover: trace it back to its table.
    std::unordered_set<size_t> covered;
    for (const auto& table : switches)
    {
        for (size_t a = table.base; a < table.base + 0x40; a += 4)
        {
            ppc_insn insn;
            ppc::Disassemble((const uint32_t*)image.Find(a), a, insn);
            if (insn.opcode != nullptr && insn.opcode->id == PPC_INST_BCTR)
            {
                covered.insert(a);
                break;
            }
        }
    }
    size_t traced = 0;
    std::vector<size_t> unresolved;
    println("# ---- TRACED JUMPTABLE ----");
    // A table bounded by an earlier one is found once that one is known, so
    // repeat until a pass finds nothing new.
    for (bool found = true; found;)
    {
        found = false;
        unresolved.clear();
        for (const auto& section : image.sections)
        {
            if (!(section.flags & SectionFlags_Code))
                continue;
            // Traces look up to 0x80 bytes back; no table sits at a section's start.
            for (size_t a = section.base + 0x100; a < section.base + section.size; a += 4)
            {
                if (ByteSwap(*(const uint32_t*)(section.data + (a - section.base))) != 0x4E800420 || covered.count(a))
                    continue;
                SwitchTable table{};
                switch (TraceBctr(image, section.base, a, switches, table))
                {
                case TraceResult::Found:
                    printTable(table);
                    switches.emplace_back(std::move(table));
                    covered.insert(a);
                    traced++;
                    found = true;
                    break;
                case TraceResult::Unresolved:
                    unresolved.push_back(a);
                    break;
                case TraceResult::NotATable:
                    break;
                }
            }
        }
    }
    fmt::println("{} jump tables ({} traced from their bctr)", switches.size(), traced);
    for (size_t a : unresolved)
        fmt::println("WARNING: bctr at {:X} reads a table that could not be resolved; add it by hand", a);

    std::ofstream f(argv[2]);
    f.write(out.data(), out.size());

    return EXIT_SUCCESS;
}
