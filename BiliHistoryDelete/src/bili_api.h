#pragma once

#include <string>
#include <vector>

struct LoginInfo {
    std::string sessdata;
    std::string buvid3;
    std::string biliJct;
    std::string dedeUserId;
};

struct HistoryItem {
    long long kid = 0;
    std::string business;
    std::string title;
    std::string author;
    long long viewAt = 0;
    std::string cover;
};

                      
// 从本地服务拉登录信息
LoginInfo FetchLoginInfo(const std::string& loginUrl);

// 构造 B 站请求头（Cookie / Referer / Origin / UA）
std::vector<std::string> BuildBiliHeaders(const LoginInfo& info);

// 分页拉取视频历史，最多 maxPages * 30 条
std::vector<HistoryItem> FetchAllHistory(const std::vector<std::string>& headers, int maxPages);

// 删除单条；成功返回 true
bool DeleteOne(const std::vector<std::string>& headers,
               const HistoryItem& item,
               const std::string& csrf);
