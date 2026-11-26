#include "strings.res.cpp"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>

static std::map<unsigned int, const char*>* gpStrings;
static std::map<unsigned int, std::string> localizedStrings;

static void loadTraditionalChinese()
{
    const char* language = std::getenv("MC2_LANGUAGE");
    if (!language || std::strcmp(language, "zh-TW") != 0)
        return;

    std::ifstream translations("localization/zh-TW/strings.tsv");
    for (std::string line; std::getline(translations, line); ) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto separator = line.find('\t');
        if (separator == std::string::npos)
            continue;

        unsigned int id = 0;
        const char* end = line.data() + separator;
        const auto result = std::from_chars(line.data(), end, id);
        if (result.ec != std::errc() || result.ptr != end || !gpStrings->count(id))
            continue;

        std::string translated = line.substr(separator + 1);
        for (size_t pos = 0; (pos = translated.find("\\n", pos)) != std::string::npos; ++pos)
            translated.replace(pos, 2, 1, '\n');
        if (!translated.empty())
            localizedStrings.emplace(id, std::move(translated));
    }
}

void /*__attribute__ ((constructor))*/ initStringResources__()
{
    gpStrings = new std::map<unsigned int, const char*>();

    int num_records = sizeof(StringRecords) / sizeof(StringRecords[0]);
    for(int i=0; i<num_records;++i) {
        const StringResRecord& rec = StringRecords[i];
        gpStrings->insert(std::make_pair((unsigned int)rec.id_, rec.str_));
    }
    loadTraditionalChinese();
}

void /*__attribute__ ((destructor))*/ freeStringResources__()
{
    localizedStrings.clear();
    gpStrings->clear();
    delete gpStrings;
}

const char* getStringById__(unsigned int id) {

    const auto localized = localizedStrings.find(id);
    if (localized != localizedStrings.end())
        return localized->second.c_str();

    if(gpStrings->count(id)) {
        return (*gpStrings)[id];
    } else {
        return 0;
    }
}

