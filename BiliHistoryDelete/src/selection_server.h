#pragma once

#include "bili_api.h"

#include <string>
#include <vector>

struct SelectionResult {
    bool submitted = false;
    int success = 0;
    int failed = 0;
};

// 启动本地 HTTP 服务并打开浏览器；
// 用户在网页点封面选区间、点“删除选中”后，直接逐条删除。
// 超时未提交返回 submitted = false。
SelectionResult RunSelectionServer(const std::vector<HistoryItem>& history,
                                   const std::vector<std::string>& headers,
                                   const std::string& csrf,
                                   int timeoutSeconds = 600);