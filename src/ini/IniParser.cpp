#include "ini/IniParser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <variant>

namespace Ini {

    namespace {

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string_view Trim(std::string_view sv) {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
                sv.remove_suffix(1);
            return sv;
        }

        std::string_view StripInlineSemicolon(std::string_view sv) {
            if (auto pos = sv.find(';'); pos != std::string_view::npos)
                sv = sv.substr(0, pos);
            return Trim(sv);
        }

    } // namespace

    const Section* Document::get(std::string_view name) const {
        auto it = sections.find(ToLower(std::string(name)));
        return it != sections.end() ? &it->second : nullptr;
    }

    std::expected<const Section*, ParseError> Document::require(std::string_view name) const {
        const Section* s = get(name);
        if (!s)
            return std::unexpected(ParseError::SectionNotFound);
        return s;
    }

    std::expected<Document, ParseError> Parse(std::string_view content) {
        Document doc;
        std::string current_section;

        std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> chain_counters;

        std::string_view remaining = content;

        while (!remaining.empty()) {
            auto newline = remaining.find('\n');
            std::string_view raw_line =
                (newline != std::string_view::npos) ? remaining.substr(0, newline) : remaining;
            remaining = (newline != std::string_view::npos) ? remaining.substr(newline + 1) : "";

            if (!raw_line.empty() && raw_line.back() == '\r')
                raw_line.remove_suffix(1);

            std::string_view line = Trim(raw_line);

            if (line.empty() || line.front() == ';' || line.front() == '#')
                continue;

            if (line.front() == '[' && line.back() == ']') {
                current_section = ToLower(std::string(line.substr(1, line.size() - 2)));
                continue;
            }

            if (current_section.empty())
                continue;

            Entry entry;

            auto comma_pos = line.find(',');
            auto equals_pos = line.find('=');

            if (comma_pos != std::string_view::npos &&
                (equals_pos == std::string_view::npos || comma_pos < equals_pos)) {
                // --- Format 1: key , value [, hash] ---
                entry.key = std::string(Trim(StripInlineSemicolon(line.substr(0, comma_pos))));

                std::string_view rest = line.substr(comma_pos + 1);
                auto second_comma = rest.find(',');

                if (second_comma != std::string_view::npos) {
                    entry.value =
                        std::string(Trim(StripInlineSemicolon(rest.substr(0, second_comma))));
                    entry.hash =
                        std::string(Trim(StripInlineSemicolon(rest.substr(second_comma + 1))));
                } else {
                    entry.value = std::string(Trim(StripInlineSemicolon(rest)));
                }
            } else if (equals_pos != std::string_view::npos) {
                // --- Format 2: key = value ---
                entry.key = std::string(Trim(line.substr(0, equals_pos)));
                std::string_view val = Trim(line.substr(equals_pos + 1));
                entry.value = std::string(StripInlineSemicolon(val));
            } else {
                entry.key = std::string(Trim(StripInlineSemicolon(line)));
            }

            if (entry.key.empty())
                continue;

            std::string key_lower = ToLower(entry.key);
            std::string prefix;
            {
                auto us = key_lower.find('_');
                auto dot = key_lower.find('.');
                auto end = std::min(us, dot);
                prefix = (end != std::string::npos) ? key_lower.substr(0, end) : key_lower;
            }

            auto& section_counters = chain_counters[current_section];
            entry.chain = section_counters[prefix];
            section_counters[prefix]++;

            doc.sections[current_section].push_back(std::move(entry));
        }

        return doc;
    }

    std::expected<Document, ParseError> ParseFile(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path))
            return std::unexpected(ParseError::FileNotFound);

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::unexpected(ParseError::ReadError);

        std::ostringstream ss;
        ss << file.rdbuf();
        if (file.fail() && !file.eof())
            return std::unexpected(ParseError::ReadError);

        return Parse(ss.str());
    }

    namespace {

        bool ParseBool(std::string_view v) {
            return v == "true" || v == "True" || v == "TRUE" || v == "1";
        }

    } // namespace

    void OptionsIni::Merge(const OptionsIni& o) {
#define MERGE_OPT(field)                                                                           \
    if (o.field.has_value())                                                                       \
    field = o.field
#define MERGE_STR(field)                                                                           \
    if (!o.field.empty())                                                                          \
    field = o.field

        MERGE_STR(keys.ctype);
        MERGE_STR(keys.key_1bl);
        MERGE_STR(keys.cpukey);
        MERGE_STR(keys.cfldv);

        MERGE_OPT(core.noenter);
        MERGE_OPT(core.nolog);
        MERGE_OPT(core.noinfo);
        MERGE_OPT(core.gxunsafe);
        MERGE_OPT(core.verbose);

        MERGE_OPT(core_builder.nosecurity);
        MERGE_OPT(core_builder.nosusecurity);
        MERGE_OPT(core_builder.noremap);
        MERGE_OPT(core_builder.nandmu);
        MERGE_OPT(core_builder.nochainpatch);
        MERGE_OPT(core_builder.nofcrt);
        MERGE_OPT(core_builder.dualpatchslots);
        MERGE_OPT(core_builder.mfg);
        MERGE_OPT(core_builder.xsb);
        MERGE_OPT(core_builder.nomobile);
        MERGE_OPT(core_builder.full_image);
        MERGE_OPT(core_builder.noecc);
        MERGE_OPT(core_builder.bigblock);

        MERGE_OPT(builder.gxunsafe);
        MERGE_OPT(builder.verbose);
        MERGE_OPT(builder.noflashfs);
        MERGE_OPT(builder.noecdremap);
        MERGE_OPT(builder.smcnocheck);
        MERGE_STR(builder.xellbutton);
        MERGE_STR(builder.xellbutton2);

        MERGE_OPT(jtag.syscall);
        MERGE_OPT(jtag.pairing_2bl);
        MERGE_OPT(jtag.cygnos);
        MERGE_OPT(jtag.demon);
        MERGE_OPT(jtag.smcnoeject);
        MERGE_OPT(jtag.smcnoblink);
        MERGE_OPT(jtag.patchsmc);
        MERGE_OPT(jtag.olddvd);
        MERGE_OPT(jtag.nodvd);
        MERGE_OPT(jtag.dualboot);

        MERGE_STR(smc_config.cputemp);
        MERGE_STR(smc_config.gputemp);
        MERGE_STR(smc_config.edramtemp);
        MERGE_STR(smc_config.overcputemp);
        MERGE_STR(smc_config.overgputemp);
        MERGE_STR(smc_config.overedramtemp);
        MERGE_STR(smc_config.cpufan);
        MERGE_STR(smc_config.gpufan);

        MERGE_STR(keyvault.avregion);
        MERGE_STR(keyvault.gameregion);
        MERGE_STR(keyvault.dvdregion);
        MERGE_STR(keyvault.macid);
        MERGE_STR(keyvault.serial);
        MERGE_STR(keyvault.consoleid);
        MERGE_STR(keyvault.osig);
        MERGE_STR(keyvault.mfdate);
        MERGE_STR(keyvault.dvdkey);

#undef MERGE_OPT
#undef MERGE_STR
    }

    std::expected<OptionsIni, OptionsError> ParseOptionsIni(std::string_view content) {
        OptionsIni opt{};

        std::string_view remaining = content;

        while (!remaining.empty()) {
            auto newline = remaining.find('\n');
            std::string_view raw =
                (newline != std::string_view::npos) ? remaining.substr(0, newline) : remaining;
            remaining = (newline != std::string_view::npos) ? remaining.substr(newline + 1) : "";

            if (!raw.empty() && raw.back() == '\r')
                raw.remove_suffix(1);

            std::string_view line = Trim(raw);

            if (line.empty() || line.front() == ';' || line.front() == '#')
                continue;

            if (line.front() == '[' && line.back() == ']')
                return std::unexpected(OptionsError::BadFormat);

            std::string_view key_sv, val_sv;
            {
                constexpr std::string_view kDelimSpaced = " = ";
                auto pos = line.find(kDelimSpaced);
                if (pos != std::string_view::npos) {
                    key_sv = Trim(line.substr(0, pos));
                    val_sv = Trim(line.substr(pos + kDelimSpaced.size()));
                } else if (auto eq = line.find('='); eq != std::string_view::npos) {
                    key_sv = Trim(line.substr(0, eq));
                    val_sv = Trim(line.substr(eq + 1));
                } else {
                    continue;
                }
            }

            val_sv = StripInlineSemicolon(val_sv);

            const std::string key = ToLower(std::string(key_sv));
            const std::string val{val_sv};

            const bool b = ParseBool(val_sv);

            if (key == "type")
                opt.keys.ctype = val;
            else if (key == "1blkey")
                opt.keys.key_1bl = val;
            else if (key == "cpukey")
                opt.keys.cpukey = val;
            else if (key == "cfldv")
                opt.keys.cfldv = val;

            else if (key == "noenter")
                opt.core.noenter = b;
            else if (key == "nolog")
                opt.core.nolog = b;
            else if (key == "noinfo")
                opt.core.noinfo = b;
            else if (key == "gxunsafe" || key == "unsafe") {
                opt.core.gxunsafe = b;
                opt.builder.gxunsafe = b;
            } else if (key == "verbose") {
                opt.core.verbose = b;
                opt.builder.verbose = b;
            }

            else if (key == "nosecurity")
                opt.core_builder.nosecurity = b;
            else if (key == "nosusecurity")
                opt.core_builder.nosusecurity = b;
            else if (key == "noremap")
                opt.core_builder.noremap = b;
            else if (key == "nandmu")
                opt.core_builder.nandmu = b;
            else if (key == "nochainpatch")
                opt.core_builder.nochainpatch = b;
            else if (key == "nofcrt")
                opt.core_builder.nofcrt = b;
            else if (key == "dualpatchslots")
                opt.core_builder.dualpatchslots = b;
            else if (key == "mfg")
                opt.core_builder.mfg = b;
            else if (key == "xsb")
                opt.core_builder.xsb = b;
            else if (key == "nomobile")
                opt.core_builder.nomobile = b;
            else if (key == "full_image")
                opt.core_builder.full_image = b;
            else if (key == "noecc")
                opt.core_builder.noecc = b;
            else if (key == "bigblock")
                opt.core_builder.bigblock = b;

            else if (key == "noflashfs")
                opt.builder.noflashfs = b;
            else if (key == "noecdremap")
                opt.builder.noecdremap = b;
            else if (key == "smcnocheck")
                opt.builder.smcnocheck = b;
            else if (key == "xellbutton")
                opt.builder.xellbutton = val;
            else if (key == "xellbutton2")
                opt.builder.xellbutton2 = val;

            else if (key == "cygnos")
                opt.jtag.cygnos = b;
            else if (key == "demon")
                opt.jtag.demon = b;
            else if (key == "smcnoeject")
                opt.jtag.smcnoeject = b;
            else if (key == "smcnoblink")
                opt.jtag.smcnoblink = b;
            else if (key == "patchsmc")
                opt.jtag.patchsmc = b;
            else if (key == "olddvd")
                opt.jtag.olddvd = b;
            else if (key == "nodvd")
                opt.jtag.nodvd = b;
            else if (key == "dualboot")
                opt.jtag.dualboot = b;
            else if (key == "jtag_syscall") {
                std::string trimmed = val;
                if (trimmed.starts_with("0x") || trimmed.starts_with("0X"))
                    trimmed = trimmed.substr(2);
                if (auto v = static_cast<uint16_t>(std::stoul(trimmed, nullptr, 16)); true)
                    opt.jtag.syscall = v;
            } else if (key == "2blpairing") {
                // format: 0x11,0x22,0x33
                std::array<uint8_t, 3> bytes{};
                std::string_view rem = val_sv;
                bool ok = true;
                for (int i = 0; i < 3 && !rem.empty(); ++i) {
                    auto comma = rem.find(',');
                    std::string_view tok =
                        Trim((comma != std::string_view::npos) ? rem.substr(0, comma) : rem);
                    rem = (comma != std::string_view::npos) ? rem.substr(comma + 1) : "";
                    if (tok.starts_with("0x") || tok.starts_with("0X"))
                        tok.remove_prefix(2);
                    try {
                        bytes[i] = static_cast<uint8_t>(std::stoul(std::string(tok), nullptr, 16));
                    } catch (...) {
                        ok = false;
                        break;
                    }
                }
                if (ok)
                    opt.jtag.pairing_2bl = bytes;
            }

            else if (key == "cputemp")
                opt.smc_config.cputemp = val;
            else if (key == "gputemp")
                opt.smc_config.gputemp = val;
            else if (key == "edramtemp")
                opt.smc_config.edramtemp = val;
            else if (key == "overcputemp")
                opt.smc_config.overcputemp = val;
            else if (key == "overgputemp")
                opt.smc_config.overgputemp = val;
            else if (key == "overedramtemp")
                opt.smc_config.overedramtemp = val;
            else if (key == "cpufan")
                opt.smc_config.cpufan = val;
            else if (key == "gpufan")
                opt.smc_config.gpufan = val;

            else if (key == "avregion" || key == "region")
                opt.keyvault.avregion = val;
            else if (key == "gameregion")
                opt.keyvault.gameregion = val;
            else if (key == "dvdregion")
                opt.keyvault.dvdregion = val;
            else if (key == "macid" || key == "mac")
                opt.keyvault.macid = val;
            else if (key == "serial")
                opt.keyvault.serial = val;
            else if (key == "consoleid")
                opt.keyvault.consoleid = val;
            else if (key == "osig")
                opt.keyvault.osig = val;
            else if (key == "mfdate")
                opt.keyvault.mfdate = val;
            else if (key == "dvdkey")
                opt.keyvault.dvdkey = val;
        }

        return opt;
    }

    OptionsResult ParseOptionsIniFile(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path))
            return std::unexpected(ParseError::FileNotFound);

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::unexpected(ParseError::ReadError);

        std::ostringstream ss;
        ss << file.rdbuf();
        if (file.fail() && !file.eof())
            return std::unexpected(ParseError::ReadError);

        auto result = ParseOptionsIni(ss.str());
        if (!result)
            return std::unexpected(result.error());
        return result.value();
    }

} // namespace Ini