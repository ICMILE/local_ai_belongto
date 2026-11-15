#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

struct HttpResponse {
    long status = 0;
    std::string body;
};

HttpResponse HttpRequest(const std::string& url,
                         const std::string& method,
                         const std::vector<std::string>& extraHeaders,
                         const std::string& postBody = "") {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string resp;
    struct curl_slist* headers = nullptr;
    for (const auto& h : extraHeaders) {
        headers = curl_slist_append(headers, h.c_str());
    }
    if (method == "POST") {
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postBody.size()));
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(rc));

    return {status, resp};
}

std::string FormatTime(long long ts) {
    time_t t = static_cast<time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

// 安全读取数字字段（可能是数字或字符串）
long long GetIntField(const json& item, const char* key) {
    if (!item.contains(key)) return 0;
    const auto& v = item[key];
    if (v.is_number()) return v.get<long long>();
    if (v.is_string()) {
        try { return std::stoll(v.get<std::string>()); } catch (...) { return 0; }
    }
    return 0;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        // ============ 1. 从本地服务获取 login info ============
        const std::string loginUrl = "http://localhost:19448/api/login";
        auto r0 = HttpRequest(loginUrl, "GET", {});
        if (r0.status != 200) {
            std::cerr << "login service HTTP " << r0.status << "\n";
            return 1;
        }

        json loginJson = json::parse(r0.body);
        std::string sessdata   = loginJson.value("SESSDATA", "");
        std::string buvid3     = loginJson.value("BUVID3", "");
        std::string biliJct    = loginJson.value("BILI_JCT", "");
        std::string dedeUserId = loginJson.value("DedeUserID", "");

        if (sessdata.empty() || biliJct.empty()) {
            std::cerr << "login info missing SESSDATA or BILI_JCT\n";
            return 1;
        }

        // ============ 2. 构造 B 站请求头（补上 Origin） ============
        std::string cookieHeader =
            "SESSDATA=" + sessdata +
            "; BUVID3=" + buvid3 +
            "; bili_jct=" + biliJct +
            "; DedeUserID=" + dedeUserId;

        std::vector<std::string> biliHeaders = {
            "Cookie: " + cookieHeader,
            "Referer: https://www.bilibili.com/history",
            "Origin: https://www.bilibili.com",
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"
        };

        // ============ 3. 取最新一条视频历史 ============
        auto r1 = HttpRequest(
            "https://api.bilibili.com/x/web-interface/history/cursor?ps=1&business=archive",
            "GET", biliHeaders);

        if (r1.status != 200) {
            std::cerr << "cursor HTTP " << r1.status << "\n";
            return 1;
        }

        json j1 = json::parse(r1.body);
        int code1 = j1.value("code", -1);
        if (code1 != 0) {
            std::cerr << "cursor failed: " << code1 << " - " << j1.value("message", "") << "\n";
            return 1;
        }

        const auto& list = j1["data"]["list"];
        if (!list.is_array() || list.empty()) {
            std::cout << "no video history\n";
            return 0;
        }

        const auto& item = list[0];
        long long kid      = GetIntField(item, "kid");
        // long long aid      = GetIntField(item, "aid");
        // std::string business = item.value("business", "");
        long long viewAt   = GetIntField(item, "view_at");
        std::string title  = item.value("title", "");
        std::string author = item.value("author_name", "");
        std::string business; 
        
        long long oid = 0;
        if (item.contains("history") && item["history"].is_object()) {
            business = item["history"].value("business", "");
            oid      = GetIntField(item["history"], "oid");
}

        std::cout << "\nlatest video history:\n"
                  << "  kid    : " << kid << "\n"
                  << "  business : " << business << "\n" 
                  << "  oid    : " << oid << "\n"
                  << "  title  : " << title << "\n"
                  << "  author : " << author << "\n"
                  << "  time   : " << FormatTime(viewAt) << "\n";


        std::cout << "\nraw item:\n" << item.dump(2) << "\n";

        // ============ 4. 确认 ============
        std::cout << "\nconfirm delete? (y/N): ";
        std::string ans;
        std::getline(std::cin, ans);
        if (ans != "y" && ans != "Y") {
            std::cout << "cancelled\n";
            return 0;
        }

        // ============ 5. 删除 ============
        std::string deleteKid = business + "_" + std::to_string(kid);
        std::string body = "kid=" + deleteKid + "&csrf=" + biliJct;

        auto r2 = HttpRequest(
            "https://api.bilibili.com/x/v2/history/delete",
            "POST", biliHeaders, body);

        if (r2.status != 200) {
            std::cerr << "delete HTTP " << r2.status << "\n";
            return 1;
        }

        json j2 = json::parse(r2.body);
        int code2 = j2.value("code", -1);
        if (code2 == 0) {
            std::cout << "deleted\n";
            return 0;
        }

        std::cerr << "delete failed: " << code2 << " - " << j2.value("message", "") << "\n";
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }
}