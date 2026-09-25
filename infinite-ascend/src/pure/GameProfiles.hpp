#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace zonai_ascend::profiles {

enum class Site : std::size_t {
    PrimarySpanSource,
    PrimarySpanHook,
    SurroundSpanSource,
    SurroundSpanHook,
    QueryValid,
    MaxHeight,
    MaxHeightRet,
    MarkerSpanSource,
    MarkerSpanHook,
    CeilingClipperPostCalc,
    ELinkSetPosition,
    ELinkSetPosAndScale,
    Count,
};

struct WordSite {
    std::ptrdiff_t offset;
    std::uint32_t original;
};

struct GameProfile {
    const char* version;
    const char* buildId;
    std::array<WordSite, static_cast<std::size_t>(Site::Count)> sites;
    std::ptrdiff_t actorPositionOffset = 0x2B4;

    constexpr const WordSite& at(Site site) const {
        return sites[static_cast<std::size_t>(site)];
    }
};

// Every original instruction must match before a profile can install hooks.
inline constexpr std::array<GameProfile, 9> kGames{{
    {"1.0.0", "082CE09B06E33A123CB1E2770F5F9147709033DB", {{
        {0x01720FAC, 0x52A83388}, {0x01720FB0, 0xBD42AF0A},
        {0x017211D0, 0x52A8339B}, {0x017211D4, 0x5283EEBC},
        {0x01720F48, 0xD10483FF}, {0x01C0FDF4, 0x1E269000},
        {0x01C0FDF8, 0xD65F03C0}, {0x00E04A90, 0xBD454503},
        {0x00E04A94, 0xBD42B6E2}, {0x00E04830, 0xA9BB7BFD},
        {0x007BDFD0, 0x79800408}, {0x01D4B674, 0xA9BD7BFD},
    }}, 0x2AC},
    {"1.1.0", "D5AD6AC71EF53E3E52417C1B81DBC9B4142AA3B3", {{
        {0x0176C484, 0x52A83388}, {0x0176C488, 0xBD42B70A},
        {0x0176C6A8, 0x52A8339B}, {0x0176C6AC, 0x5283EEBC},
        {0x0176C420, 0xD10483FF}, {0x01C72EF4, 0x1E269000},
        {0x01C72EF8, 0xD65F03C0}, {0x00E25958, 0xBD4B0903},
        {0x00E2595C, 0xBD42BEE2}, {0x00E256F0, 0xA9BB7BFD},
        {0x009FC12C, 0x79800408}, {0x01DBC11C, 0xA9BD7BFD},
    }}},
    {"1.1.2", "9A10ED9435C06733DA597D8094D9000AB5D3EE6", {{
        {0x01760DA0, 0x52A83388}, {0x01760DA4, 0xBD42B70A},
        {0x01760FC4, 0x52A8339B}, {0x01760FC8, 0x5283EEBC},
        {0x01760D3C, 0xD10483FF}, {0x01C67724, 0x1E269000},
        {0x01C67728, 0xD65F03C0}, {0x00E17154, 0xBD44E903},
        {0x00E17158, 0xBD42BEE2}, {0x00E16EEC, 0xA9BB7BFD},
        {0x009DD800, 0x79800408}, {0x01DB0D08, 0xA9BD7BFD},
    }}},
    {"1.2.0", "6F32C68DD3BC7D77AA714B80E92A096A737CDA77", {{
        {0x0174D498, 0x52A83388}, {0x0174D49C, 0xBD42B70A},
        {0x0174D6BC, 0x52A8339B}, {0x0174D6C0, 0x5283EEBC},
        {0x0174D434, 0xD10483FF}, {0x01C59394, 0x1E269000},
        {0x01C59398, 0xD65F03C0}, {0x00DFF408, 0xBD414903},
        {0x00DFF40C, 0xBD42BEE2}, {0x00DFF1A0, 0xA9BB7BFD},
        {0x007E7EF4, 0x79800408}, {0x01DA2EF4, 0xA9BD7BFD},
    }}},
    {"1.2.1", "9B4E43650501A4D4", {{
        {0x0175CA00, 0x52A83388}, {0x0175CA04, 0xBD42B70A},
        {0x0175CC24, 0x52A8339B}, {0x0175CC28, 0x5283EEBC},
        {0x0175C99C, 0xD10483FF}, {0x01C64CE4, 0x1E269000},
        {0x01C64CE8, 0xD65F03C0}, {0x00E50444, 0xBD42A903},
        {0x00E50448, 0xBD42BEE2}, {0x00E501DC, 0xA9BB7BFD},
        {0x00829FD8, 0x79800408}, {0x01DAF314, 0xA9BD7BFD},
    }}},
    {"1.4.0", "6265F94D606242CE730EF721A8037DDA8E4BFC63", {{
        {0x01D57FDC, 0x52A83388}, {0x01D57FE0, 0xBD42B70A},
        {0x01D581F4, 0x52A8339B}, {0x01D58204, 0x5283EEBC},
        {0x01D57F78, 0xD10483FF}, {0x01D579F0, 0x1E269000},
        {0x01D579F4, 0xD65F03C0}, {0x00791998, 0xBD4BB103},
        {0x0079199C, 0x794283E8}, {0x00791534, 0xA9BE7BFD},
        {0x00B8EDB0, 0x79800408}, {0x0243AA34, 0x79800408},
    }}},
    {"1.4.1", "965EAB9CEB8EB867F747DA772022C95065C9B927", {{
        {0x01D4A15C, 0x52A83388}, {0x01D4A160, 0xBD42B70A},
        {0x01D4A374, 0x52A8339B}, {0x01D4A384, 0x5283EEBC},
        {0x01D4A0F8, 0xD10483FF}, {0x01D49B70, 0x1E269000},
        {0x01D49B74, 0xD65F03C0}, {0x007D59B8, 0xBD4BED03},
        {0x007D59BC, 0x794283E8}, {0x007D5554, 0xA9BE7BFD},
        {0x007B90F0, 0x79800408}, {0x0242BC44, 0x79800408},
    }}},
    {"1.4.2", "5CB42B1CF25469FB0635FD046453D843C18BC8AB", {{
        {0x01D490BC, 0x52A83388}, {0x01D490C0, 0xBD42B70A},
        {0x01D492D4, 0x52A8339B}, {0x01D492E4, 0x5283EEBC},
        {0x01D49058, 0xD10483FF}, {0x01D48AD0, 0x1E269000},
        {0x01D48AD4, 0xD65F03C0}, {0x0080678C, 0xBD4ACD03},
        {0x00806790, 0x794283E8}, {0x00806328, 0xA9BE7BFD},
        {0x007A6DEC, 0x79800408}, {0x0242C2BC, 0x79800408},
    }}},
    {"1.4.3", "277178B7DBA1B6D4949A84778D4ABC58B31B34F5", {{
        {0x01D4C7EC, 0x52A83388}, {0x01D4C7F0, 0xBD42B70A},
        {0x01D4CA04, 0x52A8339B}, {0x01D4CA14, 0x5283EEBC},
        {0x01D4C788, 0xD10483FF}, {0x01D4C200, 0x1E269000},
        {0x01D4C204, 0xD65F03C0}, {0x0085E45C, 0xBD4BDD03},
        {0x0085E460, 0x794283E8}, {0x0085DFF8, 0xA9BE7BFD},
        {0x006D25F4, 0x79800408}, {0x024382C4, 0x79800408},
    }}},
}};

template <typename ReadWord>
const GameProfile* select(std::size_t textSize, ReadWord readWord) {
    const GameProfile* match = nullptr;
    for (const auto& game : kGames) {
        bool valid = true;
        for (const auto& site : game.sites) {
            if (site.offset < 0 ||
                static_cast<std::size_t>(site.offset) > textSize ||
                textSize - static_cast<std::size_t>(site.offset) < sizeof(std::uint32_t) ||
                readWord(site.offset) != site.original) {
                valid = false;
                break;
            }
        }
        if (valid) {
            if (match) return nullptr;
            match = &game;
        }
    }
    return match;
}

}
