#include "app.hpp"
#include "log.hpp"

#include "defines.hpp"
#include "download.hpp"
#include "location.hpp"
#include "nro.hpp"
#include "swkbd.hpp"

#include "ui/menus/film_warehouse.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/nvg_util.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"

#include <minIni.h>
#include <yyjson.h>
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <span>
#include <string_view>

namespace sphaira::ui::menu::film_warehouse {

namespace {

constexpr const char* LIBRARY_PATH = "/switch/sphaira/library.json";
constexpr const char* POSTER_DIR = "/switch/sphaira/posters";
constexpr const char* TMDB_INI = "/config/sphaira/tmdb.ini";
constexpr const char* PLAYER_INI = "/config/sphaira/film_warehouse.ini";
constexpr const char* PLAYER_DEFAULT = "/switch/nxmp/nxmp.nro";
constexpr const char* PANSOU_URL = "https://so.252035.xyz/api/search";

constexpr u32 kMaxDepth = 6;
constexpr u32 kMaxEntries = 10000;
constexpr u32 kMaxEnrich = 80;

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

bool IsVideoName(const char* name) {
    const char* ext = std::strrchr(name, '.');
    if (!ext) {
        return false;
    }
    ext++;

    static const char* exts[] = {
        "mkv", "mp4", "avi", "mov", "wmv", "flv", "ts", "m2ts", "mts",
        "webm", "rmvb", "mpg", "mpeg", "m4v", "3gp", "asf", "divx", "xvid", "ogv",
    };
    for (const char* e : exts) {
        if (iequals(ext, e)) {
            return true;
        }
    }
    return false;
}

void Collapse(std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = true;
    for (char c : s) {
        if (c == ' ') {
            if (!in_space) {
                out.push_back(' ');
            }
            in_space = true;
        } else {
            out.push_back(c);
            in_space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    s = out;
}

struct Parsed {
    std::string title;
    std::string year;
    std::string kind{"unknown"};
    std::string season;
    std::string episode;
    std::string quality;
};

// 根据文件名做媒体识别（电影 / 剧集 / 年份 / 画质），这是「入库」的核心。
Parsed ParseFilename(const std::string& filename) {
    Parsed p{};

    std::string base = filename;
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base.erase(dot);
    }
    for (auto& c : base) {
        if (c == '.' || c == '_') {
            c = ' ';
        }
    }

    std::string up = base;
    for (auto& c : up) {
        c = (char)std::toupper((unsigned char)c);
    }

    struct Range { size_t start, len; };
    std::vector<Range> ranges;

    // 1. 剧集：SxxEyy（优先）或 Sxx。
    {
        bool found = false;
        for (size_t i = 0; i + 5 < up.size(); i++) {
            if (up[i] == 'S' && std::isdigit((unsigned char)up[i+1]) && std::isdigit((unsigned char)up[i+2]) &&
                up[i+3] == 'E' && std::isdigit((unsigned char)up[i+4]) && std::isdigit((unsigned char)up[i+5])) {
                p.season = base.substr(i + 1, 2);
                p.episode = base.substr(i + 4, 2);
                ranges.push_back({i, 6});
                p.kind = "tv";
                found = true;
                break;
            }
        }
        if (!found) {
            for (size_t i = 0; i + 2 < up.size(); i++) {
                if (up[i] == 'S' && std::isdigit((unsigned char)up[i+1]) && std::isdigit((unsigned char)up[i+2]) &&
                    (i == 0 || up[i-1] == ' ')) {
                    p.season = base.substr(i + 1, 2);
                    ranges.push_back({i, 3});
                    p.kind = "tv";
                    break;
                }
            }
        }
    }

    // 2. 年份。
    {
        for (size_t i = 0; i + 3 < base.size(); i++) {
            if ((base[i] == '1' || base[i] == '2') && std::isdigit((unsigned char)base[i+1]) &&
                std::isdigit((unsigned char)base[i+2]) && std::isdigit((unsigned char)base[i+3])) {
                const bool boundary = (i == 0 || base[i-1] == ' ' || base[i-1] == '(');
                if (boundary) {
                    p.year = base.substr(i, 4);
                    size_t s = i, e = i + 4;
                    if (s > 0 && base[s-1] == '(' && e < base.size() && base[e] == ')') {
                        s--;
                        e++;
                    }
                    ranges.push_back({s, e - s});
                    break;
                }
            }
        }
    }

    // 3. 画质 / 来源标签（取第一个命中的）。
    {
        static const char* tags[] = {
            "2160P", "1080P", "720P", "480P", "4K", "8K", "BLURAY", "WEB-DL",
            "WEBRIP", "WEB RIP", "REMUX", "HDR", "DOLBY VISION", "X264", "X265",
            "HEVC", "AAC", "DTS", "DDP", "10BIT", "UHD", "BD", "HDTV", "DVDRIP", "BDRIP",
        };
        for (const char* t : tags) {
            const size_t n = std::strlen(t);
            const size_t pos = up.find(t);
            if (pos == std::string::npos) {
                continue;
            }
            const bool ok = (pos == 0 || up[pos-1] == ' ') && (pos + n >= up.size() || up[pos+n] == ' ');
            if (!ok) {
                continue;
            }
            p.quality = t;
            ranges.push_back({pos, n});
            break;
        }
        for (auto& c : p.quality) {
            c = (char)std::tolower((unsigned char)c);
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
        return a.start > b.start;
    });
    for (const auto& r : ranges) {
        if (r.start + r.len <= base.size()) {
            base.erase(r.start, r.len);
        }
    }

    Collapse(base);
    p.title = base.empty() ? filename : base;
    if (p.kind == "unknown" && !p.year.empty()) {
        p.kind = "movie";
    }

    return p;
}

const char* SourceFromMount(const std::string& mount, const std::string& fallback) {
    if (mount.find("[BAIDU]") != std::string::npos) return "百度网盘";
    if (mount.find("[GOOGLEDRIVE]") != std::string::npos) return "谷歌网盘";
    if (mount.find("[QUARK]") != std::string::npos) return "夸克网盘";
    if (mount.find("[ALIYUN]") != std::string::npos) return "阿里云盘";
    if (mount.find("[GUANGYA]") != std::string::npos) return "光鸭云盘";
    if (mount.find("[FENGLING]") != std::string::npos) return "风灵月影";
    return fallback.c_str();
}

std::string ReadTmdbKey() {
    char buf[256]{};
    ini_gets("TMDB", "api_key", "", buf, sizeof(buf), TMDB_INI);
    return std::string{buf};
}

std::string ReadPlayerPath() {
    char buf[512]{};
    ini_gets("player", "path", PLAYER_DEFAULT, buf, sizeof(buf), PLAYER_INI);
    return std::string{buf};
}

// 前向声明：GetStr / JsonStr 定义在文件靠后位置，但搜索逻辑需要提前用到。
std::string GetStr(yyjson_val* obj, const char* key);
std::string JsonStr(const std::string& s);

struct SearchResult {
    std::string title;
    std::string source;
    std::string type;
    std::string url;
    std::string password;
};

size_t SearchWriteCb(char* p, size_t s, size_t n, void* u) {
    auto* out = static_cast<std::string*>(u);
    out->append(p, s * n);
    return s * n;
}

bool HttpPostJson(const std::string& url, const std::string& body, std::string& out) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SearchWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    const auto rc = curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK && !out.empty();
}

// 调用 PanSou 资源搜索接口，返回去重后的资源链接。
bool PanSouSearch(const std::string& keyword, std::vector<SearchResult>& out) {
    out.clear();

    std::string resp;
    const std::string body = "{\"kw\":" + JsonStr(keyword) + ",\"res\":\"all\"}";
    if (!HttpPostJson(PANSOU_URL, body, resp)) {
        return false;
    }

    yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return false;
    }

    const auto code = yyjson_obj_get(root, "code");
    if (!code || !yyjson_is_int(code) || yyjson_get_int(code) != 0) {
        return false;
    }

    const auto data = yyjson_obj_get(root, "data");
    const auto results = data ? yyjson_obj_get(data, "results") : nullptr;
    if (!results || !yyjson_is_arr(results)) {
        return false;
    }

    size_t idx{}, max{};
    yyjson_val* item{};
    yyjson_arr_foreach(results, idx, max, item) {
        const std::string title = GetStr(item, "title");
        const std::string source = GetStr(item, "channel");

        const auto links = yyjson_obj_get(item, "links");
        if (!links || !yyjson_is_arr(links)) {
            continue;
        }

        size_t li{}, lm{};
        yyjson_val* link{};
        yyjson_arr_foreach(links, li, lm, link) {
            SearchResult r;
            r.title = title;
            r.source = source;
            r.type = GetStr(link, "type");
            r.url = GetStr(link, "url");
            r.password = GetStr(link, "password");
            if (r.url.empty()) {
                continue;
            }
            out.push_back(std::move(r));
        }
    }

    return !out.empty();
}

struct AiConfig {
    std::string base_url;
    std::string api_key;
    std::string model;

    auto valid() const -> bool {
        return !base_url.empty() && !model.empty();
    }
};

AiConfig ReadAiConfig() {
    AiConfig cfg;
    char buf[512]{};
    ini_gets("ai", "base_url", "", buf, sizeof(buf), PLAYER_INI);
    cfg.base_url = buf;
    ini_gets("ai", "api_key", "", buf, sizeof(buf), PLAYER_INI);
    cfg.api_key = buf;
    ini_gets("ai", "model", "", buf, sizeof(buf), PLAYER_INI);
    cfg.model = buf;
    return cfg;
}

// 读取用户指定的扫描路径（逗号/分号分隔），为空表示扫描 SD + 全部网盘。
std::vector<std::string> ReadScanPaths() {
    std::vector<std::string> paths;
    char buf[4096]{};
    ini_gets("scan", "path", "", buf, sizeof(buf), PLAYER_INI);
    const std::string str = buf;
    if (str.empty()) {
        return paths;
    }

    size_t start = 0;
    while (start <= str.size()) {
        size_t end = str.find_first_of(",;", start);
        if (end == std::string::npos) {
            end = str.size();
        }
        std::string p = str.substr(start, end - start);
        while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) {
            p.erase(p.begin());
        }
        while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) {
            p.pop_back();
        }
        if (!p.empty()) {
            paths.push_back(std::move(p));
        }
        if (end == str.size()) {
            break;
        }
        start = end + 1;
    }
    return paths;
}

bool HttpPostJsonAuth(const std::string& url, const std::string& body, const std::string& api_key, std::string& out) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!api_key.empty()) {
        h = curl_slist_append(h, ("Authorization: Bearer " + api_key).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SearchWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    const auto rc = curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK && !out.empty();
}

// 调用 OpenAI 兼容的 chat completions，返回 assistant 文本。
bool LlmChat(const AiConfig& cfg, const std::string& system, const std::string& user, std::string& out) {
    std::string url = cfg.base_url;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    url += "/chat/completions";

    std::string body = "{\"model\":" + JsonStr(cfg.model) + ",\"messages\":[";
    body += "{\"role\":\"system\",\"content\":" + JsonStr(system) + "},";
    body += "{\"role\":\"user\",\"content\":" + JsonStr(user) + "}";
    body += "]}";

    std::string resp;
    if (!HttpPostJsonAuth(url, body, cfg.api_key, resp)) {
        return false;
    }

    yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    const auto choices = root ? yyjson_obj_get(root, "choices") : nullptr;
    if (!choices || !yyjson_is_arr(choices)) {
        return false;
    }
    const auto first = yyjson_arr_get_first(choices);
    const auto message = first ? yyjson_obj_get(first, "message") : nullptr;
    const auto content = message ? yyjson_obj_get(message, "content") : nullptr;
    if (!content || !yyjson_is_str(content)) {
        return false;
    }
    out = yyjson_get_str(content);
    return !out.empty();
}

bool TmdbSearch(const std::string& api_key, const std::string& title, const std::string& year,
                const std::string& kind, std::string& out_poster_url, std::string& out_year) {
    std::string url = "https://api.themoviedb.org/3/search/";
    url += (kind == "tv") ? "tv" : "movie";
    url += "?api_key=" + curl::EscapeString(api_key);
    url += "&query=" + curl::EscapeString(title);
    if (!year.empty()) {
        url += "&year=" + year;
    }
    url += "&language=zh-CN";

    const auto res = curl::Api().ToMemory(curl::Url{url});
    if (!res.success || res.data.empty()) {
        return false;
    }

    const std::string json(res.data.begin(), res.data.end());
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    const auto results = root ? yyjson_obj_get(root, "results") : nullptr;
    if (!results || !yyjson_is_arr(results)) {
        return false;
    }

    size_t idx{}, max{};
    yyjson_val* item{};
    yyjson_arr_foreach(results, idx, max, item) {
        const auto poster = yyjson_obj_get(item, "poster_path");
        if (poster && yyjson_is_str(poster) && yyjson_get_str(poster)[0]) {
            out_poster_url = std::string("https://image.tmdb.org/t/p/w300") + yyjson_get_str(poster);
        }

        auto date = yyjson_obj_get(item, "release_date");
        if (!date) {
            date = yyjson_obj_get(item, "first_air_date");
        }
        if (date && yyjson_is_str(date) && yyjson_get_str(date)[0]) {
            out_year = std::string(yyjson_get_str(date)).substr(0, 4);
        }
        break;
    }

    return !out_poster_url.empty();
}

bool DownloadPoster(const std::string& url, const std::string& out_path) {
    const auto res = curl::Api().ToFile(curl::Url{url}, curl::Path{out_path});
    return res.success;
}

std::string GetStr(yyjson_val* obj, const char* key) {
    const auto v = obj ? yyjson_obj_get(obj, key) : nullptr;
    if (!v || !yyjson_is_str(v)) {
        return {};
    }
    const char* s = yyjson_get_str(v);
    return s ? std::string{s} : std::string{};
}

u64 GetU64(yyjson_val* obj, const char* key) {
    const auto v = obj ? yyjson_obj_get(obj, key) : nullptr;
    if (!v) {
        return 0;
    }
    if (yyjson_is_uint(v)) {
        return yyjson_get_uint(v);
    }
    if (yyjson_is_sint(v)) {
        const auto s = yyjson_get_sint(v);
        return s < 0 ? 0 : (u64)s;
    }
    return 0;
}

std::string JsonStr(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"影视仓", flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"返回", [this](){ SetPop(); }}),
        std::make_pair(Button::A, Action{"播放", [this](){ Play(); }}),
        std::make_pair(Button::X, Action{"扫描入库", [this](){ StartScan(); }}),
        std::make_pair(Button::Y, Action{"清空媒体库", [this](){ ClearLibrary(); }}),
        std::make_pair(Button::L2, Action{"打开目录", [this](){ OpenEntry(); }}),
        std::make_pair(Button::R2, Action{"搜索", [this](){ Search(); }})
    );

    LoadLibrary();
    grid::Menu::OnLayoutChange(m_list, grid::LayoutType_GridDetail);
}

Menu::~Menu() {
    for (const auto img : m_posters) {
        if (img) {
            nvgDeleteImage(App::GetVg(), img);
        }
    }
}

void Menu::LoadLibrary() {
    m_entries.clear();

    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(LIBRARY_PATH, data))) {
        return;
    }

    const std::string json(data.begin(), data.end());
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (!doc) {
        return;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    const auto arr = root ? yyjson_obj_get(root, "entries") : nullptr;
    if (!arr || !yyjson_is_arr(arr)) {
        return;
    }

    size_t idx{}, max{};
    yyjson_val* item{};
    yyjson_arr_foreach(arr, idx, max, item) {
        MediaEntry e;
        e.title = GetStr(item, "title");
        e.year = GetStr(item, "year");
        e.kind = GetStr(item, "kind");
        e.season = GetStr(item, "season");
        e.episode = GetStr(item, "episode");
        e.quality = GetStr(item, "quality");
        e.source = GetStr(item, "source");
        e.mount = GetStr(item, "mount");
        e.path = GetStr(item, "path");
        e.filename = GetStr(item, "filename");
        e.size = GetU64(item, "size");
        e.poster = GetStr(item, "poster");
        m_entries.emplace_back(std::move(e));
    }
}

void Menu::SaveLibrary() const {
    std::string json = "{\"entries\":[";
    for (size_t i = 0; i < m_entries.size(); i++) {
        const auto& e = m_entries[i];
        if (i) {
            json += ',';
        }
        json += "{\"title\":" + JsonStr(e.title);
        json += ",\"year\":" + JsonStr(e.year);
        json += ",\"kind\":" + JsonStr(e.kind);
        json += ",\"season\":" + JsonStr(e.season);
        json += ",\"episode\":" + JsonStr(e.episode);
        json += ",\"quality\":" + JsonStr(e.quality);
        json += ",\"source\":" + JsonStr(e.source);
        json += ",\"mount\":" + JsonStr(e.mount);
        json += ",\"path\":" + JsonStr(e.path);
        json += ",\"filename\":" + JsonStr(e.filename);
        json += ",\"size\":" + std::to_string(e.size);
        json += ",\"poster\":" + JsonStr(e.poster);
        json += '}';
    }
    json += "]}";

    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively("/switch/sphaira");
    fs.write_entire_file(LIBRARY_PATH, std::span<const u8>{(const u8*)json.data(), json.size()});
}

void Menu::StartScan() {
    auto entries = std::make_shared<std::vector<MediaEntry>>();

    App::Push<ProgressBox>(0, "扫描入库", "正在扫描网盘与 SD 卡...",
        [this, entries](sphaira::ui::ProgressBox* pbox) -> Result {
            *entries = DoScan(pbox);
            return 0;
        },
        [this, entries](Result rc) {
            m_entries = std::move(*entries);

            for (const auto img : m_posters) {
                if (img) {
                    nvgDeleteImage(App::GetVg(), img);
                }
            }
            m_posters.clear();
            m_posters.resize(m_entries.size(), 0);
            m_index = 0;

            SaveLibrary();
            App::Notify("入库完成，共 " + std::to_string(m_entries.size()) + " 个视频");
        }
    );
}

void Menu::ClearLibrary() {
    for (const auto img : m_posters) {
        if (img) {
            nvgDeleteImage(App::GetVg(), img);
        }
    }
    m_posters.clear();
    m_entries.clear();
    m_index = 0;

    fs::FsNativeSd fs;
    fs.DeleteFile(LIBRARY_PATH);
    App::Notify("媒体库已清空");
}

void Menu::OpenEntry() {
    if (m_index >= m_entries.size()) {
        return;
    }
    const auto& e = m_entries[m_index];

    // 定位到文件所在目录，便于在文件浏览器里与网盘交互。
    auto pos = e.path.rfind('/');
    const std::string parent = (pos == std::string::npos) ? std::string{} : e.path.substr(0, pos);

    if (!e.mount.empty()) {
        for (const auto& se : location::GetStdio(false)) {
            if (se.mount != e.mount) {
                continue;
            }
            const fs::FsPath mount{se.mount};
            auto fs = std::make_shared<fs::FsStdio>(true, mount);
            const filebrowser::FsEntry fs_entry{
                .name = fs::FsPath{se.name},
                .root = mount,
                .type = filebrowser::FsType::Stdio,
                .flags = se.flags,
            };
            const fs::FsPath start = parent.empty() ? mount : fs::FsPath{parent};
            const auto options = filebrowser::FsOption_All & ~filebrowser::FsOption_LoadAssoc;
            App::Push<filebrowser::Menu>(fs, fs_entry, start, options);
            return;
        }
        App::Notify("挂载不存在，请先登录并重启 Sphaira");
        return;
    }

    // SD 卡。
    auto fs = std::make_shared<fs::FsNativeSd>();
    const filebrowser::FsEntry fs_entry{
        .name = fs::FsPath{"SD 卡"},
        .root = fs::FsPath{"/"},
        .type = filebrowser::FsType::Sd,
        .flags = filebrowser::FsEntryFlag_IsSd,
    };
    const fs::FsPath start = parent.empty() ? fs::FsPath{"/"} : fs::FsPath{parent};
    App::Push<filebrowser::Menu>(fs, fs_entry, start, filebrowser::FsOption_All);
}

void Menu::Play() {
    if (m_index >= m_entries.size()) {
        return;
    }
    const auto& e = m_entries[m_index];

    const std::string player = ReadPlayerPath();
    if (!fs::FileExists(fs::FsPath{player})) {
        App::Notify("未找到播放器 NXMP，请安装到 /switch/nxmp/nxmp.nro，或修改 /config/sphaira/film_warehouse.ini");
        return;
    }

    // SD 卡文件：直接交给外部播放器（NXMP）。
    if (e.mount.empty()) {
        const auto rc = nro_launch(player, nro_add_arg_file(e.path));
        if (R_FAILED(rc)) {
            App::Notify("启动播放器失败");
        }
        return;
    }

    // 网盘文件：播放器进程无法访问 sphaira 的挂载点，先下载到 SD 缓存再播放。
    const fs::FsPath src{e.path};
    const fs::FsPath dst = std::string("/switch/sphaira/cache/play/") + e.filename;
    auto cloud_fs = std::make_shared<fs::FsStdio>(true, fs::FsPath{e.mount});

    App::Push<ProgressBox>(0, "下载并播放", e.filename,
        [cloud_fs, src, dst](sphaira::ui::ProgressBox* pbox) -> Result {
            fs::FsNativeSd sd;
            sd.CreateDirectoryRecursively("/switch/sphaira/cache/play");
            // 单线程拷贝：网盘 devoptab 读取是有状态的，不能并发读同一句柄。
            return pbox->CopyFile(cloud_fs.get(), &sd, src, dst, true);
        },
        [player, dst](Result rc) {
            if (R_FAILED(rc)) {
                App::Notify("下载失败，无法播放");
                return;
            }
            const auto rc2 = nro_launch(player, nro_add_arg_file(dst.toString()));
            if (R_FAILED(rc2)) {
                App::Notify("启动播放器失败");
            }
        }
    );
}

void Menu::Search() {
    std::string query;
    if (R_FAILED(swkbd::ShowText(query, "AI 搜索", "输入片名或自然语言描述", nullptr, -1, 128))) {
        return;
    }
    if (query.empty()) {
        return;
    }

    auto results = std::make_shared<std::vector<SearchResult>>();

    App::Push<ProgressBox>(0, "搜索资源", query,
        [results, query](sphaira::ui::ProgressBox* pbox) -> Result {
            std::string keyword = query;

            // 若配置了 AI，先用 LLM 从自然语言里提取片名，再搜索。
            const auto ai = ReadAiConfig();
            if (ai.valid()) {
                std::string title;
                if (LlmChat(ai, "你是影视搜索助手。请从用户的请求中提取影视片名，只返回片名本身，不要任何解释、标点或引号。", query, title) && !title.empty()) {
                    keyword = title;
                    pbox->SetTitle("已识别片名：" + title);
                }
            }

            PanSouSearch(keyword, *results);
            return 0;
        },
        [results](Result rc) {
            if (results->empty()) {
                App::Notify("未找到资源");
                return;
            }

            PopupList::Items items;
            items.reserve(results->size());
            for (const auto& r : *results) {
                items.emplace_back(r.title + "  [" + r.type + "]  " + r.source);
            }

            App::Push<PopupList>("搜索结果", items, [results](std::optional<s64> index) {
                if (!index.has_value()) {
                    return;
                }
                const auto& r = (*results)[*index];
                std::string msg = "链接: " + r.url;
                if (!r.password.empty()) {
                    msg += "  密码: " + r.password;
                }
                App::Notify(msg);
            });
        }
    );
}

std::vector<MediaEntry> Menu::DoScan(sphaira::ui::ProgressBox* pbox) {
    std::vector<MediaEntry> out;
    out.reserve(4096);

    struct Walker {
        std::vector<MediaEntry>& out;
        sphaira::ui::ProgressBox* pbox;
        std::string source;
        std::string mount;
        u32 found_since_update{};

        void Walk(fs::Fs* fs, const std::string& path, u32 depth) {
            if (pbox && pbox->ShouldExit()) return;
            if (depth > kMaxDepth) return;
            if (out.size() >= kMaxEntries) return;

            fs::Dir dir;
            if (R_FAILED(fs->OpenDirectory(path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
                return;
            }

            std::vector<FsDirectoryEntry> entries;
            if (R_FAILED(dir.ReadAll(entries))) {
                return;
            }

            std::vector<std::string> subdirs;
            subdirs.reserve(entries.size());

            for (const auto& e : entries) {
                if (pbox && pbox->ShouldExit()) return;
                if (e.name[0] == '.') {
                    continue;
                }

                const std::string child = fs::AppendPath(path, e.name);

                if (e.type == FsDirEntryType_Dir) {
                    subdirs.push_back(child);
                } else if (e.type == FsDirEntryType_File) {
                    if (out.size() >= kMaxEntries) return;
                    if (!IsVideoName(e.name)) {
                        continue;
                    }

                    auto parsed = ParseFilename(e.name);
                    MediaEntry me;
                    me.title = std::move(parsed.title);
                    me.year = std::move(parsed.year);
                    me.kind = std::move(parsed.kind);
                    me.season = std::move(parsed.season);
                    me.episode = std::move(parsed.episode);
                    me.quality = std::move(parsed.quality);
                    me.source = source;
                    me.mount = mount;
                    me.path = child;
                    me.filename = e.name;
                    me.size = 0;
                    out.push_back(std::move(me));

                    if (pbox && (++found_since_update % 50 == 0)) {
                        pbox->NewTransfer("已发现 " + std::to_string(out.size()) + " 个视频");
                    }
                }
            }

            for (const auto& d : subdirs) {
                Walk(fs, d, depth + 1);
                if (pbox && pbox->ShouldExit()) return;
                if (out.size() >= kMaxEntries) return;
            }
        }
    };

    // 1. 若用户指定了扫描路径，只扫描这些路径（SD 卡上）。
    const auto scan_paths = ReadScanPaths();
    if (!scan_paths.empty()) {
        fs::FsNativeSd sd;
        for (const auto& p : scan_paths) {
            if (pbox && pbox->ShouldExit()) break;
            Walker w{out, pbox, "SD 卡", ""};
            w.Walk(&sd, p, 0);
        }
    } else {
        // 默认：扫描 SD 卡根目录。
        fs::FsNativeSd sd;
        Walker w{out, pbox, "SD 卡", ""};
        w.Walk(&sd, "/", 0);

        // 2. 所有已挂载的网盘 / USB / HDD 等 stdio 设备。
        for (const auto& e : location::GetStdio(false)) {
            if (pbox && pbox->ShouldExit()) break;
            if (e.fs_hidden) {
                continue;
            }

            const std::string source = SourceFromMount(e.mount, e.name);
            auto fs = std::make_shared<fs::FsStdio>(true, fs::FsPath{e.mount});
            Walker w{out, pbox, source, e.mount};
            w.Walk(fs.get(), e.mount, 0);
        }
    }

    // 3. AI 入库：用 TMDB 为去重后的标题刮削海报与年份（可选，需自备 API key）。
    const std::string api_key = ReadTmdbKey();
    if (!api_key.empty() && !out.empty()) {
        if (pbox) pbox->NewTransfer("正在刮削元数据...");

        fs::FsNativeSd sd;
        sd.CreateDirectoryRecursively(POSTER_DIR);

        std::map<std::string, std::string> poster_cache;
        std::set<std::string> attempted;
        u32 enriched = 0;

        for (auto& e : out) {
            if (pbox && pbox->ShouldExit()) break;
            if (enriched >= kMaxEnrich) break;

            const std::string key = e.kind + "|" + e.title + "|" + e.year;

            const auto it = poster_cache.find(key);
            if (it != poster_cache.end()) {
                e.poster = it->second;
                continue;
            }
            if (attempted.count(key)) {
                continue;
            }
            attempted.insert(key);

            std::string poster_url, tmdb_year;
            if (!TmdbSearch(api_key, e.title, e.year, e.kind, poster_url, tmdb_year)) {
                continue;
            }

            u64 h = 5381;
            for (const char c : key) {
                h = h * 33 + (unsigned char)c;
            }
            const std::string local = std::string(POSTER_DIR) + "/" + std::to_string(h) + ".jpg";

            if (DownloadPoster(poster_url, local)) {
                e.poster = local;
                if (!tmdb_year.empty()) {
                    e.year = tmdb_year;
                }
                poster_cache[key] = local;
                enriched++;
                if (pbox) {
                    pbox->NewTransfer("正在刮削元数据 " + std::to_string(enriched));
                }
            }
        }
    }

    return out;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (!(touch && m_index == i)) {
            m_index = i;
        }
    });

    this->SetSubHeading(std::to_string(m_entries.empty() ? 0 : m_index + 1) + " / " + std::to_string(m_entries.size()));
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "媒体库为空，请按 X 扫描入库");
        return;
    }

    if (m_posters.size() != m_entries.size()) {
        m_posters.resize(m_entries.size(), 0);
    }

    fs::FsNativeSd sd;

    m_list->Draw(vg, theme, m_entries.size(), [this, &sd](auto* vg, auto* theme, auto v, auto pos) {
        const auto& e = m_entries[pos];
        const bool selected = pos == m_index;

        int image = 0;
        if (pos < m_posters.size()) {
            if (!m_posters[pos] && !e.poster.empty()) {
                std::vector<u8> data;
                if (R_SUCCEEDED(sd.read_entire_file(e.poster, data)) && !data.empty()) {
                    m_posters[pos] = nvgCreateImageMem(vg, 0, data.data(), (int)data.size());
                }
            }
            image = m_posters[pos];
        }

        std::string author = e.year;
        if (!e.source.empty()) {
            if (!author.empty()) {
                author += " · ";
            }
            author += e.source;
        }

        const char* kind = (e.kind == "movie") ? "电影" : (e.kind == "tv") ? "剧集" : "视频";
        std::string meta = e.quality;
        if (!meta.empty()) {
            meta += " · ";
        }
        meta += kind;
        if (!e.season.empty()) {
            meta += " S" + e.season;
            if (!e.episode.empty()) {
                meta += "E" + e.episode;
            }
        }

        DrawEntry(vg, theme, grid::LayoutType_GridDetail, v, selected, image, e.title.c_str(), author.c_str(), meta.c_str());
    });
}

} // namespace sphaira::ui::menu::film_warehouse
