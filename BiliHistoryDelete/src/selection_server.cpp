#ifdef _WIN32
#include <winsock2.h> 
#endif

#include "selection_server.h"
#include "utils.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <conio.h>       // _kbhit / _getch
#include <shellapi.h>
#endif

using json = nlohmann::json;

namespace {

std::string BuildSelectHtml(const std::vector<HistoryItem>& history, int port) {
    json arr = json::array();
    for (const auto& h : history) {
        arr.push_back({
            {"kid",    h.business + "_" + std::to_string(h.kid)},
            {"title",  h.title},
            {"author", h.author},
            {"time",   FormatTime(h.viewAt)},
            {"cover",  h.cover}
        });
    }

    std::string jsonStr = arr.dump();
    std::string safe;
    for (size_t i = 0; i < jsonStr.size(); i++) {
        if (jsonStr[i] == '<' && i + 1 < jsonStr.size() && jsonStr[i + 1] == '/') {
            safe += "<\\/"; i++;
        } else safe += jsonStr[i];
    }

    std::ostringstream os;
    os << R"(<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="referrer" content="no-referrer">
<title>选择要删除的历史记录</title>
<style>
 body{font-family:sans-serif;background:#f6f7f8;margin:0;padding:16px;padding-bottom:100px}
 .status-bar{position:sticky;top:0;background:#fff;padding:12px;margin:-16px -16px 16px -16px;
             box-shadow:0 2px 8px rgba(0,0,0,.08);z-index:100;font-size:14px}
 .status-bar .hint{color:#666}
 .status-bar .count{color:#fb7299;font-weight:bold}
 .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px}
 .card{background:#fff;border-radius:8px;overflow:hidden;cursor:pointer;
       border:3px solid transparent;transition:border-color .1s,opacity .2s;position:relative}
 .card img{width:100%;aspect-ratio:16/9;object-fit:cover;display:block;background:#ddd}
 .card .meta{padding:6px 8px;font-size:12px;color:#555}
 .card .title{font-size:13px;color:#111;margin-bottom:4px;display:-webkit-box;
              -webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
 .card.selected{border-color:#fb7299}
 .card.selected .meta{background:#fff0f5}
 .card.select-start{border-color:#2196f3}
 .card.select-end{border-color:#ff9800}
 .card.deselect-start{border-color:#9c27b0}
 .card.deselect-end{border-color:#f44336}
 .card.deleted{opacity:.25;pointer-events:none;filter:grayscale(1)}
 .card.deleted::after{content:'已删除';position:absolute;top:8px;right:8px;
                      background:#000;color:#fff;font-size:12px;padding:2px 8px;border-radius:4px}
 .card .badge{position:absolute;top:6px;left:6px;padding:2px 8px;border-radius:4px;
              font-size:12px;color:#fff;display:none;font-weight:bold}
 .card.select-start .badge,
 .card.select-end .badge,
 .card.deselect-start .badge,
 .card.deselect-end .badge{display:block}
 .card.select-start .badge{background:#2196f3}
 .card.select-end .badge{background:#ff9800}
 .card.deselect-start .badge{background:#9c27b0}
 .card.deselect-end .badge{background:#f44336}
 .bar{position:fixed;bottom:0;left:0;right:0;background:#fff;padding:12px;
      box-shadow:0 -2px 8px rgba(0,0,0,.1);display:flex;gap:12px;align-items:center}
 .bar button{padding:8px 16px;font-size:14px;cursor:pointer;border:1px solid #ddd;
             border-radius:4px;background:#fff}
 .bar button.active{background:#fb7299;color:#fff;border-color:#fb7299}
 .bar .count{font-weight:bold;color:#fb7299}
 .bar .total{color:#666;font-size:13px;margin-left:8px}
</style></head><body>
<div class="status-bar">
  <span class="hint" id="hint"></span>
  <span style="margin-left:16px">已选 <span class="count" id="count">0</span> 条</span>
  <span class="total" id="total"></span>
</div>
<div class="grid" id="grid"></div>
<div class="bar">
  <button id="selectMode" class="active">选择区间</button>
  <button id="deselectMode">取消区间</button>
  <button id="selAll">全选</button>
  <button id="clearAll">清空</button>
  <button id="export" style="background:#fb7299;color:#fff;border:none;margin-left:auto">
    删除选中</button>
</div>
<script>
const items = )" << safe << R"(;
const PORT  = )" << port << R"(;
const grid = document.getElementById('grid');
const countEl = document.getElementById('count');
const totalEl = document.getElementById('total');
const hintEl = document.getElementById('hint');
const btnSelect = document.getElementById('selectMode');
const btnDeselect = document.getElementById('deselectMode');
const selected = new Set();
const deleted = new Set();
const cards = [];

let mode = 'select';
let rangeStart = -1, rangeEnd = -1;
let deselectStart = -1, deselectEnd = -1;
let totalSuccess = 0, totalFailed = 0;

function refreshBadge(idx) {
  const c = cards[idx];
  const badge = c.querySelector('.badge');
  if (c.classList.contains('deselect-start')) badge.textContent = 'C';
  else if (c.classList.contains('deselect-end')) badge.textContent = 'D';
  else if (c.classList.contains('select-start')) badge.textContent = 'A';
  else if (c.classList.contains('select-end')) badge.textContent = 'B';
  else badge.textContent = '';
}

function updateHint() {
  if (mode === 'deselect') {
    hintEl.textContent = deselectStart === -1
      ? '取消模式：点击要取消的起点 C'
      : '取消模式：起点 C = [' + deselectStart + ']，点击终点 D 取消该区间';
    return;
  }
  if (rangeStart === -1) hintEl.textContent = '点击封面选择起点 A（Ctrl+点击单独切换；ESC 退出程序）';
  else if (rangeEnd === -1) hintEl.textContent = '起点 A = [' + rangeStart + ']，点击终点 B 选中区间';
  else hintEl.textContent = '区间 [' + Math.min(rangeStart, rangeEnd) + ' - '
                          + Math.max(rangeStart, rangeEnd) + '] 已选中，点【取消区间】可移除 C-D 段';
}
function updateCount() { countEl.textContent = selected.size; }
function updateTotal() {
  if (totalSuccess + totalFailed > 0)
    totalEl.textContent = '累计：成功 ' + totalSuccess + '，失败 ' + totalFailed;
}

function setSelected(idx, on) {
  if (deleted.has(items[idx].kid)) return;
  if (on) { selected.add(items[idx].kid); cards[idx].classList.add('selected'); }
  else    { selected.delete(items[idx].kid); cards[idx].classList.remove('selected'); }
}

function clearMarks() {
  rangeStart = -1; rangeEnd = -1;
  cards.forEach((c, i) => {
    c.classList.remove('select-start', 'select-end');
    refreshBadge(i);
  });
}
function clearCDMarks() {
  deselectStart = -1; deselectEnd = -1;
  cards.forEach((c, i) => {
    c.classList.remove('deselect-start', 'deselect-end');
    refreshBadge(i);
  });
}
function clearAll() {
  selected.clear();
  cards.forEach(c => c.classList.remove('selected'));
  clearMarks(); clearCDMarks();
  updateCount(); updateHint();
}
function selectRange(a, b) {
  const s = Math.min(a, b), e = Math.max(a, b);
  for (let i = s; i <= e; i++) setSelected(i, true);
}
function deselectRange(c, d) {
  const s = Math.min(c, d), e = Math.max(c, d);
  for (let i = s; i <= e; i++) setSelected(i, false);
}
function switchMode(m) {
  mode = m;
  btnSelect.classList.toggle('active', m === 'select');
  btnDeselect.classList.toggle('active', m === 'deselect');
  clearCDMarks();
  updateHint();
}

items.forEach((it, idx) => {
  const card = document.createElement('div');
  card.className = 'card';
  card.dataset.kid = it.kid;
  card.innerHTML =
    '<img src="' + it.cover + '" loading="lazy" alt="">' +
    '<span class="badge"></span>' +
    '<div class="meta"><div class="title">' +
    it.title.replace(/</g,'&lt;') + '</div><div>' +
    it.author + ' · ' + it.time + '</div></div>';

  card.onclick = (e) => {
    if (deleted.has(it.kid)) return;
    if (e.ctrlKey || e.metaKey) {
      setSelected(idx, !selected.has(it.kid));
      updateCount(); return;
    }
    if (mode === 'deselect') {
      if (deselectStart === -1) {
        deselectStart = idx;
        card.classList.add('deselect-start'); refreshBadge(idx); updateHint();
      } else {
        deselectEnd = idx;
        card.classList.add('deselect-end'); refreshBadge(idx);
        deselectRange(deselectStart, deselectEnd);
        updateCount();
        setTimeout(() => { clearCDMarks(); switchMode('select'); }, 400);
      }
      return;
    }
    if (rangeStart === -1) {
      rangeStart = idx;
      card.classList.add('select-start'); refreshBadge(idx); updateHint();
    } else if (rangeEnd === -1) {
      rangeEnd = idx;
      card.classList.add('select-end'); refreshBadge(idx);
      selectRange(rangeStart, rangeEnd);
      updateCount(); updateHint();
    } else {
      clearAll();
      rangeStart = idx;
      card.classList.add('select-start'); refreshBadge(idx); updateHint();
    }
  };

  cards.push(card);
  grid.appendChild(card);
});

btnSelect.onclick = () => switchMode('select');
btnDeselect.onclick = () => switchMode('deselect');
document.getElementById('selAll').onclick = () => {
  items.forEach((it, idx) => setSelected(idx, true));
  clearMarks(); clearCDMarks(); updateCount(); updateHint();
};
document.getElementById('clearAll').onclick = clearAll;

document.getElementById('export').onclick = async () => {
  const list = items.filter(x => selected.has(x.kid) && !deleted.has(x.kid));
  if (!list.length) return alert('没有选中项（或选中的都已删除）');

  const btn = document.getElementById('export');
  btn.disabled = true; btn.textContent = '删除中...';
  try {
    const res = await fetch('http://127.0.0.1:' + PORT + '/submit', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(list)
    });
    const j = await res.json();
    if (j.error) { alert('错误：' + j.error); }
    else {
      (j.deletedKids || []).forEach(kid => {
        deleted.add(kid);
        selected.delete(kid);
        const idx = items.findIndex(x => x.kid === kid);
        if (idx >= 0) {
          cards[idx].classList.remove('selected', 'select-start', 'select-end');
          cards[idx].classList.add('deleted');
        }
      });
      totalSuccess = j.totalSuccess;
      totalFailed = j.totalFailed;
      updateCount(); updateTotal(); clearMarks();
      alert('本批：成功 ' + j.success + '，失败 ' + j.failed +
            '\n累计：成功 ' + j.totalSuccess + '，失败 ' + j.totalFailed +
            '\n\n可继续选择其他视频；按 ESC 退出程序');
    }
  } catch (err) {
    alert('提交失败：' + err.message);
  }
  btn.disabled = false; btn.textContent = '删除选中';
};

switchMode('select');
updateHint();
</script></body></html>)";
    return os.str();
}

} // namespace

SelectionResult RunSelectionServer(const std::vector<HistoryItem>& history,
                                   const std::vector<std::string>& headers,
                                   const std::string& csrf,
                                   int timeoutSeconds) {
    SelectionResult result;

    httplib::Server svr;
    std::mutex delMutex;       // 保证同一时刻只有一批在删
    std::string html;

    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(html, "text/html; charset=utf-8");
    });

    svr.Post("/submit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(delMutex);
        try {
            json arr = json::parse(req.body);
            std::vector<HistoryItem> todo;
            std::vector<std::string> kids;

            for (const auto& e : arr) {
                HistoryItem h;
                h.title = e.value("title", "");
                std::string kid = e.value("kid", "");
                auto pos = kid.find('_');
                if (pos == std::string::npos) continue;
                h.business = kid.substr(0, pos);
                try { h.kid = std::stoll(kid.substr(pos + 1)); } catch (...) { continue; }
                todo.push_back(h);
                kids.push_back(kid);
            }

            int success = 0, failed = 0;
            json deletedKids = json::array();

            for (size_t i = 0; i < todo.size(); i++) {
                std::cout << "[" << i + 1 << "/" << todo.size() << "] "
                          << todo[i].title << " ... " << std::flush;
                bool ok = DeleteOne(headers, todo[i], csrf);
                if (!ok) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    ok = DeleteOne(headers, todo[i], csrf);
                }
                std::cout << (ok ? "OK\n" : "FAILED\n");
                if (ok) { success++; deletedKids.push_back(kids[i]); }
                else    { failed++; }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            result.success += success;
            result.failed  += failed;
            if (success + failed > 0) result.submitted = true;

            std::cout << "batch: " << success << " ok / " << failed << " failed"
                      << "  (total " << result.success << " / " << result.failed << ")\n"
                      << "press ESC to quit, or keep selecting in browser\n\n";

            res.set_content(json{
                {"success", success},
                {"failed", failed},
                {"deletedKids", deletedKids},
                {"totalSuccess", result.success},
                {"totalFailed", result.failed}
            }.dump(), "application/json");
        } catch (std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    int port = svr.bind_to_any_port("127.0.0.1");
    if (port <= 0) throw std::runtime_error("failed to bind local port");

    html = BuildSelectHtml(history, port);

    std::thread t([&]() { svr.listen_after_bind(); });

    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::cout << "\nopening browser: " << url << "\n"
              << "press ESC in this console to quit\n\n";
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif

    // 主线程轮询 ESC
    while (true) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 27) {   // ESC
                std::cout << "\nESC pressed, waiting for current batch to finish...\n";
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 等当前删除批次释放锁，再停服务
    {
        std::lock_guard<std::mutex> lock(delMutex);
    }
    svr.stop();
    if (t.joinable()) t.join();

    std::cout << "total: " << result.success << " deleted, "
              << result.failed << " failed\n";
    return result;
}