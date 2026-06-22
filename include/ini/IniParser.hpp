#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Ini {

    enum class ParseError {
        FileNotFound,
        ReadError,
        SectionNotFound,
        MalformedEntry,
    };

    constexpr std::string_view ParseErrorString(ParseError e) {
        switch (e) {
            case ParseError::FileNotFound:
                return "File not found";
            case ParseError::ReadError:
                return "Failed to read file";
            case ParseError::SectionNotFound:
                return "Section not found";
            case ParseError::MalformedEntry:
                return "Malformed entry";
        }
        return "Unknown";
    }

    struct Entry {
        std::string key;
        std::string value;
        std::string hash; // may be empty
        uint8_t chain{0};
    };

    using Section = std::vector<Entry>;

    struct Document {
        std::unordered_map<std::string, Section> sections;

        [[nodiscard]] const Section* get(std::string_view name) const;

        [[nodiscard]] std::expected<const Section*, ParseError>
        require(std::string_view name) const;
    };

    [[nodiscard]] std::expected<Document, ParseError> Parse(std::string_view content);
    [[nodiscard]] std::expected<Document, ParseError> ParseFile(const std::filesystem::path& path);

    enum class OptionsError {
        BadFormat,
    };

    constexpr std::string_view OptionsErrorString(OptionsError e) {
        switch (e) {
            case OptionsError::BadFormat:
                return "Incorrect formatting in options.ini";
        }
        return "Unknown";
    }

    struct GxBuildKeys {
        std::string ctype;
        std::string key_1bl;
        std::string cpukey;
        std::string cfldv;
    };

    struct CoreOptions {
        std::optional<bool> noenter;
        std::optional<bool> nolog;
        std::optional<bool> noinfo;
        std::optional<bool> gxunsafe;
        std::optional<bool> verbose;
    };

    struct CoreBuilderOptions {
        std::optional<bool> nosecurity;
        std::optional<bool> nosusecurity;
        std::optional<bool> noremap;
        std::optional<bool> nandmu;
        std::optional<bool> nochainpatch;
        std::optional<bool> nofcrt;
        std::optional<bool> dualpatchslots;
        std::optional<bool> mfg;
        std::optional<bool> xsb;
        std::optional<bool> nomobile;
        std::optional<bool> full_image;
        std::optional<bool> noecc;
        std::optional<bool> bigblock;
    };

    struct BuilderOptions {
        std::optional<bool> gxunsafe;
        std::optional<bool> verbose;
        std::optional<bool> noflashfs;
        std::optional<bool> noecdremap;
        std::optional<bool> smcnocheck;
        std::string xellbutton;
        std::string xellbutton2;
    };

    struct JtagOptions {
        std::optional<uint16_t> syscall;
        std::optional<std::array<uint8_t, 3>> pairing_2bl;
        std::optional<bool> cygnos;
        std::optional<bool> demon;
        std::optional<bool> smcnoeject;
        std::optional<bool> smcnoblink;
        std::optional<bool> patchsmc;
        std::optional<bool> olddvd;
        std::optional<bool> nodvd;
        std::optional<bool> dualboot;
    };

    struct SmcConfigOptions {
        std::string cputemp;
        std::string gputemp;
        std::string edramtemp;
        std::string overcputemp;
        std::string overgputemp;
        std::string overedramtemp;
        std::string cpufan;
        std::string gpufan;
    };

    struct KeyvaultOptions {
        std::string avregion;
        std::string gameregion;
        std::string dvdregion;
        std::string macid;
        std::string serial;
        std::string consoleid;
        std::string osig;
        std::string mfdate;
        std::string dvdkey;
    };

    struct OptionsIni {
        GxBuildKeys keys;
        CoreOptions core;
        CoreBuilderOptions core_builder;
        BuilderOptions builder;
        JtagOptions jtag;
        SmcConfigOptions smc_config;
        KeyvaultOptions keyvault;

        void Merge(const OptionsIni& other);
    };

    [[nodiscard]] std::expected<OptionsIni, OptionsError> ParseOptionsIni(std::string_view content);

    using OptionsResult = std::expected<OptionsIni, std::variant<ParseError, OptionsError>>;
    [[nodiscard]] OptionsResult ParseOptionsIniFile(const std::filesystem::path& path);

} // namespace Ini