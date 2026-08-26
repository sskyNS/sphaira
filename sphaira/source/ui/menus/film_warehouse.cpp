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
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <thread>

namespace sphaira::ui::menu::film_warehouse {

namespace {

constexpr const char* LIBRARY_PATH = "/switch/sphaira/library.json";
constexpr const char* POSTER_DIR = "/switch/sphaira/posters";
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

// 规范化用户填写的 OpenAI 兼容 base URL：去掉首尾空白、尾部斜杠，
// 以及多余的 /chat/completions 后缀，避免拼成 .../chat/completions/chat/completions (404)。
std::string NormalizeBaseUrl(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    s = s.substr(start);

    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }

    constexpr std::string_view suffix = "/chat/completions";
    if (s.size() >= suffix.size() && iequals(std::string_view{s}.substr(s.size() - suffix.size()), suffix)) {
        s.erase(s.size() - suffix.size());
        while (!s.empty() && s.back() == '/') {
            s.pop_back();
        }
    }

    return s;
}

// 去掉粘贴 API key 时混入的空白字符（空格 / tab / 换行），避免保存到错误值。
std::string SanitizeApiKey(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        out.push_back((char)c);
    }
    return out;
}

AiConfig ReadAiConfig() {
    AiConfig cfg;
    char buf[512]{};
    ini_gets("ai", "base_url", "", buf, sizeof(buf), PLAYER_INI);
    cfg.base_url = NormalizeBaseUrl(buf);
    ini_gets("ai", "api_key", "", buf, sizeof(buf), PLAYER_INI);
    cfg.api_key = SanitizeApiKey(buf);
    ini_gets("ai", "model", "", buf, sizeof(buf), PLAYER_INI);
    cfg.model = buf;

    // 测试用硬编码默认值（DeepSeek），仅在 ini 未配置时兜底。
    // TODO: 测试完成后移除，改为仅读取 ini。
    if (cfg.base_url.empty()) {
        cfg.base_url = "https://api.deepseek.com/v1";
    }
    if (cfg.api_key.empty()) {
        cfg.api_key = "sk-bcc7991df00e451ab592df37a27f96b4";
    }
    if (cfg.model.empty()) {
        cfg.model = "deepseek-v4-pro";
    }
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
        // MiMo / Azure-OpenAI 等兼容服务读的是 api-key 头，而不是 Authorization。
        h = curl_slist_append(h, ("api-key: " + api_key).c_str());
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

// 使用 mediary-scout 作者部署的公共 TMDB 代理（无需自备 TMDB key）：
//   - 元数据：https://tmdb-proxy.mediaryscout.app/search/...
//   - 海报图：https://tmdb-proxy.mediaryscout.app/img/t/p/w342/<poster_path>
// 该代理在服务端注入作者的 TMDB key，并解决了 image.tmdb.org 在大陆被墙的问题。
bool TmdbSearch(const std::string& title, const std::string& year,
                const std::string& kind, std::string& out_poster_url, std::string& out_year,
                std::string* out_overview = nullptr) {
    std::string url = "https://tmdb-proxy.mediaryscout.app/search/";
    url += (kind == "tv") ? "tv" : "movie";
    url += "?query=" + curl::EscapeString(title);
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
            out_poster_url = std::string("https://tmdb-proxy.mediaryscout.app/img/t/p/w342") + yyjson_get_str(poster);
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

// ========================================================================
// 入库（转存到网盘）辅助
// ========================================================================
namespace {

constexpr const char* ACQUIRE_PATH = "/switch/sphaira/acquired.json";

std::string GetNowStr() {
    const auto t = std::time(nullptr);
    const auto tm = std::localtime(&t);
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    return buf;
}

std::string ExtractBdstoken(const std::string& cookie) {
    std::string upper = cookie;
    for (auto& c : upper) {
        c = (char)std::toupper((unsigned char)c);
    }
    const size_t pos = upper.find("BDSTOKEN=");
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + 9;
    const size_t end = cookie.find(';', start);
    return cookie.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// 光鸭离线下载（转存磁力 / ed2k / http）。返回是否成功提交任务。
bool GuangyaOfflineDownload(const std::string& url) {
    char buf[512]{};
    ini_gets("GUANGYA", "access_token", "", buf, sizeof(buf), "/config/sphaira/mount/guangya.ini");
    const std::string token = buf;
    if (token.empty()) {
        return false;
    }

    const std::string body = "{\"url\":\"" + url + "\",\"parentId\":\"\",\"newName\":\"\"}";

    std::string resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, ("Authorization: Bearer " + token).c_str());
    h = curl_slist_append(h, "Dt: 4");
    h = curl_slist_append(h, "origin: https://www.guangyapan.com");
    h = curl_slist_append(h, "referer: https://www.guangyapan.com/");
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.guangyapan.com/cloudcollection/v1/create_task");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SearchWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);

    yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const auto root = yyjson_doc_get_root(doc);
    const auto msg = root ? yyjson_obj_get(root, "msg") : nullptr;
    return !msg || !yyjson_is_str(msg) || std::string{yyjson_get_str(msg)} == "success";
}

// 百度离线下载（转存）。返回 errno==0 表示提交成功。
bool BaiduOfflineDownload(const std::string& url) {
    char buf[512]{};
    ini_gets("BAIDU", "bdstoken", "", buf, sizeof(buf), "/config/sphaira/mount/baidu.ini");
    std::string bdstoken = buf;
    if (bdstoken.empty()) {
        ini_gets("BAIDU", "cookie", "", buf, sizeof(buf), "/config/sphaira/mount/baidu.ini");
        bdstoken = ExtractBdstoken(buf);
    }
    if (bdstoken.empty()) {
        return false;
    }

    const std::string body = "method=add_task&app_id=250528&source_url=" + curl::EscapeString(url) +
        "&save_path=" + curl::EscapeString("/") + "&bdstoken=" + curl::EscapeString(bdstoken);

    std::string resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    curl_slist* h = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_URL, "https://pan.baidu.com/rest/2.0/services/cloud_dl");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SearchWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);

    yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const auto root = yyjson_doc_get_root(doc);
    const auto errno_val = root ? yyjson_obj_get(root, "errno") : nullptr;
    return errno_val && yyjson_is_int(errno_val) && yyjson_get_int(errno_val) == 0;
}

struct OfflineDrive {
    std::string label;
    std::string section;
};

std::vector<OfflineDrive> ListOfflineDrives() {
    std::vector<OfflineDrive> out;
    char buf[512]{};

    ini_gets("GUANGYA", "access_token", "", buf, sizeof(buf), "/config/sphaira/mount/guangya.ini");
    if (buf[0]) {
        out.push_back({"光鸭网盘", "GUANGYA"});
    }

    ini_gets("BAIDU", "bdstoken", "", buf, sizeof(buf), "/config/sphaira/mount/baidu.ini");
    if (!buf[0]) {
        ini_gets("BAIDU", "cookie", "", buf, sizeof(buf), "/config/sphaira/mount/baidu.ini");
    }
    if (buf[0]) {
        out.push_back({"百度网盘", "BAIDU"});
    }

    return out;
}

bool SubmitOfflineDownload(const std::string& section, const std::string& url) {
    if (section == "GUANGYA") {
        return GuangyaOfflineDownload(url);
    }
    if (section == "BAIDU") {
        return BaiduOfflineDownload(url);
    }
    return false;
}

// ---- 夸克网盘转存分享链接（4 步：token → detail → save → poll） ----
// 夸克没有磁力/离线下载的 web API，只能转存「分享链接」。
constexpr const char* QUARK_SHARE_BASE = "https://drive-pc.quark.cn";
constexpr const char* QUARK_SHARE_QUERY = "pr=ucpro&fr=pc&uc_param_str=";
constexpr const char* QUARK_SHARE_UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
    " (KHTML, like Gecko) quark-cloud-drive/2.5.20 Chrome/100.0.4896.160"
    " Electron/18.3.5.4-b478491100 Safari/537.36 Channel/pckk_other_ch";

std::string QuarkReadCookie() {
    char buf[4096]{};
    ini_gets("QUARK", "cookie", "", buf, sizeof(buf), "/config/sphaira/mount/quark.ini");
    return buf;
}

curl_slist* QuarkHeaders(const std::string& cookie) {
    curl_slist* h = nullptr;
    h = curl_slist_append(h, ("Cookie: " + cookie).c_str());
    h = curl_slist_append(h, ("User-Agent: " + std::string(QUARK_SHARE_UA)).c_str());
    h = curl_slist_append(h, "Referer: https://pan.quark.cn/");
    h = curl_slist_append(h, "Origin: https://pan.quark.cn");
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Accept: application/json, text/plain, */*");
    return h;
}

// 通用夸克请求（GET 时 body 传空串）。
std::string QuarkRequest(const std::string& url, const std::string& method, const std::string& body, const std::string& cookie) {
    std::string resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }
    curl_slist* h = QuarkHeaders(cookie);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SearchWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }
    curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return resp;
}

// 从分享链接提取 pwd_id 与 passcode。
bool ParseQuarkShareUrl(const std::string& url, const std::string& password, std::string& pwd_id, std::string& passcode) {
    const auto s_pos = url.find("/s/");
    if (s_pos == std::string::npos) {
        return false;
    }
    const size_t id_start = s_pos + 3;
    const size_t id_end = url.find_first_of("?#", id_start);
    pwd_id = url.substr(id_start, id_end == std::string::npos ? std::string::npos : id_end - id_start);
    if (pwd_id.empty()) {
        return false;
    }

    passcode = password;
    const auto extract = [&url](const char* key) -> std::string {
        const std::string m1 = std::string("?") + key + "=";
        const std::string m2 = std::string("&") + key + "=";
        size_t p = url.find(m1);
        if (p == std::string::npos) {
            p = url.find(m2);
        }
        if (p == std::string::npos) {
            return {};
        }
        p += (url.find(m1) != std::string::npos ? m1.length() : m2.length());
        const size_t e = url.find_first_of("&#", p);
        return url.substr(p, e == std::string::npos ? std::string::npos : e - p);
    };
    if (passcode.empty()) {
        passcode = extract("passcode");
    }
    if (passcode.empty()) {
        passcode = extract("pwd");
    }
    return true;
}

// 夸克转存：把分享链接里的文件保存到「我的网盘根目录」。返回是否成功。
bool QuarkSaveShare(const std::string& url, const std::string& password) {
    const std::string cookie = QuarkReadCookie();
    if (cookie.empty()) {
        return false;
    }

    std::string pwd_id, passcode;
    if (!ParseQuarkShareUrl(url, password, pwd_id, passcode)) {
        return false;
    }

    // 1. 获取 stoken
    std::string body = "{\"pwd_id\":" + JsonStr(pwd_id) + ",\"passcode\":" + JsonStr(passcode) + "}";
    std::string resp = QuarkRequest(std::string(QUARK_SHARE_BASE) + "/1/clouddrive/share/sharepage/token?" + QUARK_SHARE_QUERY,
        "POST", body, cookie);

    std::string stoken;
    {
        yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
        if (!doc) {
            return false;
        }
        ON_SCOPE_EXIT(yyjson_doc_free(doc));
        const auto root = yyjson_doc_get_root(doc);
        const auto data = root ? yyjson_obj_get(root, "data") : nullptr;
        const auto v = data ? yyjson_obj_get(data, "stoken") : nullptr;
        stoken = v && yyjson_is_str(v) ? yyjson_get_str(v) : "";
    }
    if (stoken.empty()) {
        return false;
    }

    // 2. 获取分享详情（fid + share_fid_token）
    const std::string detail_url = std::string(QUARK_SHARE_BASE) + "/1/clouddrive/share/sharepage/detail?" + QUARK_SHARE_QUERY +
        "&pwd_id=" + curl::EscapeString(pwd_id) + "&stoken=" + curl::EscapeString(stoken) +
        "&pdir_fid=0&force=0&_page=1&_size=50&_fetch_banner=0&_fetch_share=0&_fetch_total=1"
        "&_sort=file_type:asc,updated_at:desc";
    resp = QuarkRequest(detail_url, "GET", "", cookie);

    std::vector<std::string> fid_list;
    std::vector<std::string> fid_token_list;
    {
        yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
        if (!doc) {
            return false;
        }
        ON_SCOPE_EXIT(yyjson_doc_free(doc));
        const auto root = yyjson_doc_get_root(doc);
        const auto data = root ? yyjson_obj_get(root, "data") : nullptr;
        const auto list = data ? yyjson_obj_get(data, "list") : nullptr;
        if (list && yyjson_is_arr(list)) {
            size_t idx{}, max{};
            yyjson_val* item{};
            yyjson_arr_foreach(list, idx, max, item) {
                const auto fid = GetStr(item, "fid");
                const auto token = GetStr(item, "share_fid_token");
                if (!fid.empty() && !token.empty()) {
                    fid_list.emplace_back(fid);
                    fid_token_list.emplace_back(token);
                }
            }
        }
    }
    if (fid_list.empty()) {
        return false;
    }

    // 3. 保存到根目录
    std::string fid_arr = "[", token_arr = "[";
    for (size_t i = 0; i < fid_list.size(); i++) {
        if (i) {
            fid_arr += ',';
            token_arr += ',';
        }
        fid_arr += JsonStr(fid_list[i]);
        token_arr += JsonStr(fid_token_list[i]);
    }
    fid_arr += ']';
    token_arr += ']';

    body = "{\"fid_list\":" + fid_arr + ",\"fid_token_list\":" + token_arr +
        ",\"to_pdir_fid\":\"0\",\"pwd_id\":" + JsonStr(pwd_id) + ",\"stoken\":" + JsonStr(stoken) +
        ",\"pdir_fid\":\"0\",\"scene\":\"link\"}";
    resp = QuarkRequest(std::string(QUARK_SHARE_BASE) + "/1/clouddrive/share/sharepage/save?" + QUARK_SHARE_QUERY,
        "POST", body, cookie);

    std::string task_id;
    {
        yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
        if (!doc) {
            return false;
        }
        ON_SCOPE_EXIT(yyjson_doc_free(doc));
        const auto root = yyjson_doc_get_root(doc);
        const auto data = root ? yyjson_obj_get(root, "data") : nullptr;
        const auto v = data ? yyjson_obj_get(data, "task_id") : nullptr;
        task_id = v && yyjson_is_str(v) ? yyjson_get_str(v) : "";
    }
    if (task_id.empty()) {
        return false;
    }

    // 4. 轮询任务直到完成
    for (int i = 0; i < 12; i++) {
        const std::string task_url = std::string(QUARK_SHARE_BASE) + "/1/clouddrive/task?" + QUARK_SHARE_QUERY +
            "&task_id=" + curl::EscapeString(task_id) + "&retry_index=" + std::to_string(i);
        resp = QuarkRequest(task_url, "GET", "", cookie);

        yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
        if (!doc) {
            return false;
        }
        ON_SCOPE_EXIT(yyjson_doc_free(doc));
        const auto root = yyjson_doc_get_root(doc);
        const auto data = root ? yyjson_obj_get(root, "data") : nullptr;
        const auto status = data ? yyjson_obj_get(data, "status") : nullptr;
        if (status && yyjson_is_int(status) && yyjson_get_int(status) == 2) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
    return false;
}

struct AcquireEntry {
    std::string title;
    std::string drive;
    std::string time;
};

std::vector<AcquireEntry> LoadAcquireLog() {
    std::vector<AcquireEntry> out;

    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(ACQUIRE_PATH, data))) {
        return out;
    }

    const std::string json(data.begin(), data.end());
    yyjson_doc* doc = yyjson_read(json.c_str(), json.size(), 0);
    if (!doc) {
        return out;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    const auto arr = root ? yyjson_obj_get(root, "entries") : nullptr;
    if (!arr || !yyjson_is_arr(arr)) {
        return out;
    }

    size_t idx{}, max{};
    yyjson_val* item{};
    yyjson_arr_foreach(arr, idx, max, item) {
        AcquireEntry e;
        e.title = GetStr(item, "title");
        e.drive = GetStr(item, "drive");
        e.time = GetStr(item, "time");
        if (!e.title.empty()) {
            out.emplace_back(std::move(e));
        }
    }
    return out;
}

void RecordAcquire(const std::string& title, const std::string& drive) {
    auto entries = LoadAcquireLog();

    AcquireEntry e;
    e.title = title;
    e.drive = drive;
    e.time = GetNowStr();
    entries.emplace_back(std::move(e));

    std::string json = "{\"entries\":[";
    for (size_t i = 0; i < entries.size(); i++) {
        if (i) {
            json += ',';
        }
        json += "{\"title\":" + JsonStr(entries[i].title);
        json += ",\"drive\":" + JsonStr(entries[i].drive);
        json += ",\"time\":" + JsonStr(entries[i].time);
        json += '}';
    }
    json += "]}";

    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively("/switch/sphaira");
    fs.write_entire_file(ACQUIRE_PATH, std::span<const u8>{(const u8*)json.data(), json.size()});
}

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"影视仓", flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"返回", [this](){ SetPop(); }}),
        std::make_pair(Button::A, Action{"播放", [this](){ Play(); }}),
        std::make_pair(Button::X, Action{"扫描入库", [this](){ StartScan(); }}),
        std::make_pair(Button::Y, Action{"清空媒体库", [this](){ ClearLibrary(); }}),
        std::make_pair(Button::L2, Action{"打开目录", [this](){ OpenEntry(); }}),
        std::make_pair(Button::R2, Action{"搜索", [this](){ Search(); }}),
        std::make_pair(Button::R3, Action{"AI 设置", [this](){ AiConfig(); }}),
        std::make_pair(Button::L3, Action{"入库记录", [this](){ ShowAcquireLog(); }})
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

                // 夸克只支持「转存分享链接」，磁力/ed2k 则走光鸭/百度的离线下载。
                const bool is_quark = r.url.find("pan.quark.cn/s/") != std::string::npos || r.type == "quark";
                const auto drives = ListOfflineDrives();

                PopupList::Items actions;
                if (is_quark) {
                    actions.emplace_back("转存到夸克网盘");
                }
                for (const auto& d : drives) {
                    actions.emplace_back("入库到" + d.label);
                }
                actions.emplace_back("复制链接");

                App::Push<PopupList>(r.title, actions, [r, drives, is_quark](std::optional<s64> ai) {
                    if (!ai.has_value()) {
                        return;
                    }
                    s64 v = *ai;

                    if (is_quark) {
                        if (v == 0) {
                            if (QuarkSaveShare(r.url, r.password)) {
                                RecordAcquire(r.title, "夸克网盘");
                                App::Notify("已转存到夸克网盘，请到网盘查看");
                            } else {
                                App::Notify("转存失败，请检查夸克登录凭证或分享链接");
                            }
                            return;
                        }
                        --v;
                    }

                    if (v < (s64)drives.size()) {
                        const auto& d = drives[v];
                        if (SubmitOfflineDownload(d.section, r.url)) {
                            RecordAcquire(r.title, d.label);
                            App::Notify("已提交离线下载到" + d.label + "，请到网盘查看");
                        } else {
                            App::Notify("入库失败，请检查" + d.label + "登录凭证");
                        }
                        return;
                    }

                    std::string msg = "链接: " + r.url;
                    if (!r.password.empty()) {
                        msg += "  密码: " + r.password;
                    }
                    App::Notify(msg);
                });
            });
        }
    );
}

void Menu::AiConfig() {
    const auto set_field = [this](const char* key, const char* title, const char* guide) {
        char buf[512]{};
        ini_gets("ai", key, "", buf, sizeof(buf), PLAYER_INI);
        const std::string cur = buf;

        std::string out;
        if (R_FAILED(swkbd::ShowText(out, title, guide, cur.empty() ? nullptr : cur.c_str(), 0, 512))) {
            return;
        }

        fs::FsNativeSd().CreateDirectoryRecursively("/config/sphaira");
        ini_puts("ai", key, out.c_str(), PLAYER_INI);
        App::Notify("AI 配置已保存");
    };

    App::Push<PopupList>("AI 设置",
        PopupList::Items{"设置 Base URL", "设置 API Key", "设置模型", "测试连接"},
        [this, set_field](std::optional<s64> index) {
            if (!index.has_value()) {
                return;
            }
            switch (*index) {
                case 0: set_field("base_url", "Base URL", "OpenAI 兼容接口，如 https://api.openai.com/v1"); break;
                case 1: set_field("api_key", "API Key", "云端服务必填，本地模型可留空"); break;
                case 2: set_field("model", "模型", "如 gpt-4o-mini / deepseek-chat"); break;
                case 3: TestAiConnection(); break;
            }
        });
}

void Menu::TestAiConnection() {
    const auto cfg = ReadAiConfig();
    if (!cfg.valid()) {
        App::Notify("请先配置 Base URL 和模型");
        return;
    }

    auto result = std::make_shared<std::string>();
    App::Push<ProgressBox>(0, "测试连接", "正在请求 AI 接口...",
        [cfg, result](sphaira::ui::ProgressBox* pbox) -> Result {
            std::string out;
            const bool ok = LlmChat(cfg, "You are a helpful assistant.", "ping", out);
            if (ok) {
                *result = "连接成功：" + out;
            } else {
                *result = "连接失败，请检查 Base URL / API Key / 模型名";
            }
            return 0;
        },
        [result](Result rc) {
            App::Notify(*result);
        });
}

void Menu::ShowAcquireLog() {
    const auto entries = LoadAcquireLog();
    if (entries.empty()) {
        App::Notify("暂无入库记录");
        return;
    }

    PopupList::Items items;
    items.reserve(entries.size());
    for (const auto& e : entries) {
        items.emplace_back(e.title + "  [" + e.drive + "]  " + e.time);
    }

    App::Push<PopupList>("入库记录", items, [](std::optional<s64>) {});
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

    // 3. 刮削海报与年份：走作者公共 TMDB 代理，无需自备 API key。
    if (!out.empty()) {
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
            if (!TmdbSearch(e.title, e.year, e.kind, poster_url, tmdb_year)) {
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
