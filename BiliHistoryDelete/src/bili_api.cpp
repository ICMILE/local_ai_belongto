#include "bili_api.h"
#include "http_client.h"
#include "utils.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>

LoginInfo FetchLoginInfo(const std::string& loginUrl) {
    auto r = HttpRequest(loginUrl, "GET", {});
    if (r.status != 200) throw std::runtime_error("login service HTTP " + std::to_string(r.status));

    json j = json::parse(r.body);
    LoginInfo info;
    info.sessdata   = j.value("SESSDATA", "");
    info.buvid3     = j.value("BUVID3", "");
    info.biliJct    = j.value("BILI_JCT", "");
    info.dedeUserId = j.value("DedeUserID", "");
    if (info.sessdata.empty() || info.biliJct.empty())
        throw std::runtime_error("login info missing SESSDATA or BILI_JCT");
    return info;
}

std::vector<std::string> BuildBiliHeaders(const LoginInfo& info) {
    std::string cookie =
        "SESSDATA=" + info.sessdata + "; BUVID3=" + info.buvid3 +
        "; bili_jct=" + info.biliJct + "; DedeUserID=" + info.dedeUserId;

    return {
        "Cookie: " + cookie,
        "Referer: https://www.bilibili.com/history",
        "Origin: https://www.bilibili.com",
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"
    };
}

std::vector<HistoryItem> FetchAllHistory(const std::vector<std::string>& headers, int maxPages) {
    std::vector<HistoryItem> all;
    std::string cursorMax, cursorViewAt;

    for (int p = 0; p < maxPages; p++) {
        std::string url = "https://api.bilibili.com/x/web-interface/history/cursor?ps=30&business=archive";
        if (!cursorMax.empty()) url += "&max=" + cursorMax + "&view_at=" + cursorViewAt;

        auto r = HttpRequest(url, "GET", headers);
        if (r.status != 200) { std::cerr << "cursor HTTP " << r.status << "\n"; break; }

        json j = json::parse(r.body);
        if (j.value("code", -1) != 0) {
            std::cerr << "cursor failed: " << j.value("code", -1)
                      << " - " << j.value("message", "") << "\n";
            break;
        }

        const auto& list = j["data"]["list"];
        if (!list.is_array() || list.empty()) break;

        for (const auto& item : list) {
            HistoryItem h;
            h.kid    = GetIntField(item, "kid");
            h.viewAt = GetIntField(item, "view_at");
            h.title  = item.value("title", "");
            h.author = item.value("author_name", "");
            if (item.contains("history") && item["history"].is_object()) {
                h.business = item["history"].value("business", "");
                h.cover    = item["history"].value("cover", "");
            }
            if (h.cover.empty()) h.cover = item.value("cover", "");
            all.push_back(h);
        }

        const auto& cursor = j["data"]["cursor"];
        cursorMax    = std::to_string(cursor.value("max", 0LL));
        cursorViewAt = std::to_string(cursor.value("view_at", 0LL));
        if (cursor.value("is_end", false)) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    return all;
}

bool DeleteOne(const std::vector<std::string>& headers,
               const HistoryItem& item,
               const std::string& csrf) {
    std::string deleteKid = item.business.empty()
        ? std::to_string(item.kid)
        : item.business + "_" + std::to_string(item.kid);
    std::string body = "kid=" + deleteKid + "&csrf=" + csrf;

    auto r = HttpRequest("https://api.bilibili.com/x/v2/history/delete", "POST", headers, body);
    if (r.status != 200) return false;
    try { return json::parse(r.body).value("code", -1) == 0; } catch (...) { return false; }
}