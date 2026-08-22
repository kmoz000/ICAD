#define WEBVIEW_HEADER
#include <webview/webview.h>

#include "icad/json/value.hpp"
#include "icad/viewer_source.hpp"
#include "icad/viewer/live_session.hpp"

#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace {

constexpr std::string_view workbench_html = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ICAD Live Workbench</title><style>
:root{color-scheme:dark;--bg:#0b1020;--panel:#11182c;--line:#27324a;--text:#e6edf7;--muted:#91a0b9;--accent:#56a8ff;--ok:#51d88a;--bad:#ff6b7a}
*{box-sizing:border-box}html,body{margin:0;width:100%;height:100%;overflow:hidden;background:var(--bg);color:var(--text);font:13px system-ui,-apple-system,sans-serif}
body{display:grid;grid-template-rows:48px 40px 1fr}.toolbar{display:flex;align-items:center;gap:10px;padding:8px 12px;border-bottom:1px solid var(--line);background:#0d1426;-webkit-user-select:none;user-select:none}
.macos .toolbar{padding-left:86px}.exportbar{display:flex;align-items:center;gap:8px;padding:5px 12px;border-bottom:1px solid var(--line);background:#10182a}.exportbar label{font-weight:700;color:var(--muted);font-size:11px;letter-spacing:.06em}.export-path{min-width:120px;flex:1;border:1px solid #33415e;border-radius:6px;background:#09101f;color:var(--text);padding:6px 9px;font:12px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;outline:0}.export-path:focus{border-color:var(--accent)}
.app-logo{width:28px;height:28px;object-fit:contain;flex:0 0 auto}.brand{font-weight:750;letter-spacing:.08em}.path{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--muted);flex:1}
button{border:1px solid #3a4968;border-radius:7px;background:#18233b;color:var(--text);padding:7px 12px;font-weight:650;cursor:pointer}button:hover{border-color:var(--accent)}button.primary{background:#1266b5;border-color:#338de0}
.status{font-variant-numeric:tabular-nums;color:var(--muted)}.status.ok{color:var(--ok)}.status.bad{color:var(--bad)}
.main{display:grid;grid-template-columns:minmax(340px,40%) 1fr;min-height:0}.source-pane{display:grid;grid-template-rows:1fr minmax(92px,24%);min-width:0;border-right:1px solid var(--line)}
textarea{width:100%;height:100%;resize:none;border:0;outline:0;padding:16px;background:#0c1324;color:#dce8fb;font:13px/1.55 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;tab-size:2;white-space:pre}
.diagnostics{overflow:auto;border-top:1px solid var(--line);background:var(--panel);padding:8px}.empty{color:var(--muted);padding:6px}.diagnostic{display:block;width:100%;text-align:left;margin:0 0 6px;padding:7px 9px;border:1px solid #553341;background:#281722;color:#ffd7dd;font:12px/1.35 ui-monospace,monospace}.diagnostic strong{color:#ff8794}
.preview{position:relative;min-width:0;background:#111827;overflow:hidden}.preview canvas{display:block;width:100%;height:100%;border:0;background:#111827}.placeholder{position:absolute;inset:0;display:grid;place-items:center;color:var(--muted);pointer-events:none}.placeholder[hidden]{display:none}.metrics{font-size:12px;color:var(--muted)}
.icad-viewer-controls{position:absolute;z-index:2;top:12px;left:12px;display:flex;gap:8px;align-items:center;padding:8px;background:#ffffffdd;border-radius:9px}.icad-semantic-tree{position:absolute;z-index:2;right:12px;top:12px;max-height:calc(100% - 24px);overflow:auto;display:flex;flex-direction:column;gap:5px;padding:10px;min-width:190px;background:#ffffffea;border:1px solid #94a3b8;border-radius:9px;color:#102030}.icad-semantic-tree button[aria-pressed=true]{background:#fbbf24}.icad-measurement{font-size:12px;max-width:240px;margin-bottom:4px}.icad-viewer-controls button,.icad-viewer-controls select,.icad-semantic-tree button{color:#102030;background:#fff;border-color:#64748b}
@media(max-width:820px){body{grid-template-rows:auto auto 1fr}.toolbar{flex-wrap:wrap}.macos .toolbar{padding-left:86px}.main{grid-template-columns:1fr;grid-template-rows:48% 52%}.source-pane{border-right:0;border-bottom:1px solid var(--line)}}
</style></head><body>
<header class="toolbar"><img id="app-logo" class="app-logo" alt="ICAD"><span class="brand">ICAD LIVE</span><span id="path" class="path"></span><span id="metrics" class="metrics"></span><span id="status" class="status">Starting…</span><button id="compile">Compile</button><button id="save" class="primary">Save</button></header>
<div class="exportbar"><label for="export-path">EXPORT FOLDER</label><input id="export-path" class="export-path" type="text" spellcheck="false"><button id="export">Export package</button></div>
<main class="main"><section class="source-pane"><textarea id="source" aria-label="ICAD source editor" spellcheck="false"></textarea><div id="diagnostics" class="diagnostics"><div class="empty">Diagnostics will appear here.</div></div></section><section id="preview" class="preview"><div id="placeholder" class="placeholder">Compiling 3D preview…</div></section></main>
<script>
const ui={logo:document.getElementById('app-logo'),source:document.getElementById('source'),path:document.getElementById('path'),status:document.getElementById('status'),metrics:document.getElementById('metrics'),diagnostics:document.getElementById('diagnostics'),preview:document.getElementById('preview'),placeholder:document.getElementById('placeholder'),compile:document.getElementById('compile'),save:document.getElementById('save'),exportPath:document.getElementById('export-path'),export:document.getElementById('export')};
let timer=0,request=0,dirty=false,mountedViewer=null;
function status(text,kind=''){ui.status.textContent=text;ui.status.className='status '+kind}
function jump(line,column){const lines=ui.source.value.split('\n');let offset=0;for(let i=0;i<Math.max(0,line-1)&&i<lines.length;i++)offset+=lines[i].length+1;offset+=Math.max(0,column-1);ui.source.focus();ui.source.setSelectionRange(offset,Math.min(offset+1,ui.source.value.length))}
function showDiagnostics(items){ui.diagnostics.replaceChildren();if(!items.length){const empty=document.createElement('div');empty.className='empty';empty.textContent='No compiler diagnostics.';ui.diagnostics.append(empty);return}for(const item of items){const button=document.createElement('button');button.className='diagnostic';button.innerHTML='<strong>'+item.code+'</strong> · '+item.line+':'+item.column+' · '+item.message.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));button.addEventListener('click',()=>jump(item.line,item.column));ui.diagnostics.append(button)}}
function mountModel(model){if(mountedViewer)pauseMountedViewer();ui.preview.replaceChildren();const canvas=document.createElement('canvas');canvas.id='viewer';canvas.setAttribute('aria-label','Live ICAD 3D preview');ui.preview.append(canvas);mountedViewer=window.ICADViewer.mount(canvas,model);ui.placeholder=null}
function pauseMountedViewer(){try{mountedViewer.pause()}catch(_){}mountedViewer=null}
async function compile(){const token=++request;clearTimeout(timer);status('Compiling…');ui.compile.disabled=true;try{const result=await window.icadPreview(ui.source.value);if(token!==request||result.stale)return;showDiagnostics(result.diagnostics||[]);if(result.success){if(result.model)mountModel(result.model);else if(ui.placeholder)ui.placeholder.hidden=true;const speed=result.unchanged?'cached':`${Math.max(0,result.milliseconds).toFixed(0)} ms · ${result.recomputedBodies} rebuilt/${result.reusedBodies} reused · ${result.parallelWorkers} workers`;ui.metrics.textContent=`${result.bodies} components · ${speed}`;status(dirty?'Preview ready · unsaved':'Preview ready','ok')}else{status(result.message||'Compile failed','bad')}}catch(error){if(token===request)status(String(error),'bad')}finally{if(token===request)ui.compile.disabled=false}}
function schedule(){dirty=true;status('Editing · unsaved');clearTimeout(timer);timer=setTimeout(compile,120)}
async function save(){ui.save.disabled=true;try{const result=await window.icadSave(ui.source.value);if(result.success){dirty=false;status('Saved','ok');await compile()}else status(result.message||'Save failed','bad')}catch(error){status(String(error),'bad')}finally{ui.save.disabled=false}}
async function exportPackage(){const destination=ui.exportPath.value.trim();if(!destination){status('Choose an export folder','bad');ui.exportPath.focus();return}ui.export.disabled=true;status('Exporting complete package…');try{const result=await window.icadExport(ui.source.value,destination);showDiagnostics(result.diagnostics||[]);if(result.success){ui.exportPath.value=result.directory;status(`Exported ${result.artifacts} files in ${result.milliseconds.toFixed(0)} ms`,'ok')}else status(result.message||'Export failed','bad')}catch(error){status(String(error),'bad')}finally{ui.export.disabled=false}}
ui.source.addEventListener('input',schedule);ui.compile.addEventListener('click',compile);ui.save.addEventListener('click',save);ui.export.addEventListener('click',exportPackage);ui.exportPath.addEventListener('keydown',event=>{if(event.key==='Enter')exportPackage()});window.addEventListener('keydown',event=>{if((event.metaKey||event.ctrlKey)&&event.key.toLowerCase()==='s'){event.preventDefault();save()}if((event.metaKey||event.ctrlKey)&&event.shiftKey&&event.key.toLowerCase()==='e'){event.preventDefault();exportPackage()}});
(async()=>{try{const initial=await window.icadRead();document.documentElement.classList.add(initial.platform);const iconBytes=new Uint8Array((initial.iconHex.match(/../g)||[]).map(value=>Number.parseInt(value,16)));ui.logo.src=URL.createObjectURL(new Blob([iconBytes],{type:'image/png'}));ui.path.textContent=initial.path;ui.source.value=initial.source;ui.exportPath.value=initial.defaultExportDirectory;dirty=false;await compile()}catch(error){status(String(error),'bad')}})();
</script></body></html>)HTML";

[[nodiscard]] auto file_url(const std::filesystem::path& path) -> std::string {
    const auto absolute = std::filesystem::absolute(path).lexically_normal().generic_string();
    std::string encoded;
    encoded.reserve(absolute.size() + 16);
    constexpr char hex[] = "0123456789ABCDEF";
    for (const char raw_character : absolute) {
        const auto character = static_cast<unsigned char>(raw_character);
        const bool safe = std::isalnum(character) != 0 || character == '/' || character == ':' ||
                          character == '-' || character == '_' || character == '.' ||
                          character == '~';
        if (safe) {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(character >> 4U) & 0x0FU]);
            encoded.push_back(hex[character & 0x0FU]);
        }
    }
#ifdef _WIN32
    return "file:///" + encoded;
#else
    return "file://" + encoded;
#endif
}

auto usage(std::ostream& output) -> void {
    output << "Usage: icad-viewer <source.icad|compiled.html>\n"
              "Open an ICAD source with live editing and 3D preview, or a compiled HTML viewer.\n";
}

struct Context {
    webview_t window{};
    icad::viewer::LiveSession* session{};
    struct PreviewJob {
        std::string id;
        std::string source;
    };
    std::mutex preview_mutex;
    std::condition_variable_any preview_ready;
    std::optional<PreviewJob> pending_preview;
    std::jthread preview_worker;
    std::jthread export_worker;
};

[[nodiscard]] auto string_arguments(std::string_view request, std::size_t count)
    -> std::optional<std::vector<std::string>> {
    const auto parsed = icad::json::parse(request);
    if (!parsed.ok())
        return std::nullopt;
    const auto* arguments = parsed.value->array();
    if (arguments == nullptr || arguments->size() != count)
        return std::nullopt;
    std::vector<std::string> result;
    result.reserve(count);
    for (const auto& argument : *arguments) {
        const auto* value = argument.string();
        if (value == nullptr)
            return std::nullopt;
        result.push_back(*value);
    }
    return result;
}

[[nodiscard]] auto first_string(std::string_view request) -> std::optional<std::string> {
    auto arguments = string_arguments(request, 1);
    return arguments ? std::optional<std::string>{std::move(arguments->front())} : std::nullopt;
}

[[nodiscard]] auto diagnostics_value(const std::vector<icad::compiler::Diagnostic>& source)
    -> icad::json::Value::Array {
    icad::json::Value::Array diagnostics;
    diagnostics.reserve(source.size());
    for (const auto& diagnostic : source) {
        diagnostics.emplace_back(icad::json::Value::Object{
            {"code", diagnostic.code},
            {"message", diagnostic.message},
            {"line", static_cast<double>(diagnostic.location.line)},
            {"column", static_cast<double>(diagnostic.location.column)},
        });
    }
    return diagnostics;
}

[[nodiscard]] auto preview_value(const icad::viewer::PreviewResult& preview) -> icad::json::Value {
    return icad::json::Value::Object{
        {"success", preview.success},
        {"message", preview.message},
        {"revision", static_cast<double>(preview.revision)},
        {"bodies", static_cast<double>(preview.bodies)},
        {"materials", static_cast<double>(preview.materials)},
        {"scenes", static_cast<double>(preview.scenes)},
        {"keyframes", static_cast<double>(preview.keyframes)},
        {"reusedBodies", static_cast<double>(preview.reused_bodies)},
        {"recomputedBodies", static_cast<double>(preview.recomputed_bodies)},
        {"parallelWorkers", static_cast<double>(preview.parallel_workers)},
        {"milliseconds", preview.milliseconds},
        {"unchanged", preview.unchanged},
        {"diagnostics", diagnostics_value(preview.diagnostics)},
    };
}

[[nodiscard]] auto preview_payload(const icad::viewer::PreviewResult& preview) -> std::string {
    auto payload = icad::json::serialize(preview_value(preview));
    if (preview.success && !preview.model_json.empty()) {
        payload.pop_back();
        payload += ",\"model\":";
        payload += preview.model_json;
        payload.push_back('}');
    }
    return payload;
}

[[nodiscard]] auto package_value(const icad::viewer::PackageResult& package)
    -> icad::json::Value {
    return icad::json::Value::Object{
        {"success", package.success},
        {"message", package.message},
        {"directory", package.directory.string()},
        {"artifacts", static_cast<double>(package.artifacts)},
        {"components", static_cast<double>(package.components)},
        {"solids", static_cast<double>(package.solids)},
        {"milliseconds", package.milliseconds},
        {"diagnostics", diagnostics_value(package.diagnostics)},
    };
}

auto respond(Context& context, const char* id, const icad::json::Value& value) -> void {
    const auto serialized = icad::json::serialize(value);
    webview_return(context.window, id, 0, serialized.c_str());
}

auto fail_request(Context& context, const char* id, std::string_view message) -> void {
    const auto serialized = icad::json::quote(message);
    webview_return(context.window, id, 1, serialized.c_str());
}

struct AsyncReply {
    std::string id;
    std::string payload;
};

auto return_async(webview_t window, void* argument) -> void {
    const std::unique_ptr<AsyncReply> reply{static_cast<AsyncReply*>(argument)};
    webview_return(window, reply->id.c_str(), 0, reply->payload.c_str());
}

auto dispatch_reply(Context& context, std::string id, const icad::json::Value& value) -> void {
    auto reply = std::make_unique<AsyncReply>(
        AsyncReply{std::move(id), icad::json::serialize(value)});
    auto* raw = reply.release();
    if (webview_dispatch(context.window, return_async, raw) != WEBVIEW_ERROR_OK)
        delete raw;
}

auto dispatch_reply(Context& context, std::string id, std::string payload) -> void {
    auto reply = std::make_unique<AsyncReply>(AsyncReply{std::move(id), std::move(payload)});
    auto* raw = reply.release();
    if (webview_dispatch(context.window, return_async, raw) != WEBVIEW_ERROR_OK)
        delete raw;
}

auto start_preview_worker(Context& context) -> void {
    context.preview_worker = std::jthread([&context](std::stop_token stop) {
        while (!stop.stop_requested()) {
            Context::PreviewJob job;
            {
                std::unique_lock lock{context.preview_mutex};
                context.preview_ready.wait(lock, stop,
                                           [&context] { return context.pending_preview.has_value(); });
                if (stop.stop_requested())
                    return;
                job = std::move(*context.pending_preview);
                context.pending_preview.reset();
            }
            const auto preview = context.session->preview(job.source);
            dispatch_reply(context, std::move(job.id), preview_payload(preview));
        }
    });
}

auto stop_workers(Context& context) -> void {
    context.preview_worker.request_stop();
    context.preview_ready.notify_all();
    if (context.preview_worker.joinable())
        context.preview_worker.join();
    context.export_worker.request_stop();
    if (context.export_worker.joinable())
        context.export_worker.join();
}

[[nodiscard]] auto platform_name() -> std::string_view {
#ifdef __APPLE__
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

auto read_binding(const char* id, const char*, void* argument) -> void {
    auto& context = *static_cast<Context*>(argument);
    respond(context, id,
            icad::json::Value::Object{{"path", context.session->source_path().string()},
                                      {"source", context.session->source()},
                                      {"iconHex", std::string{icad::viewer::icon_png_hex}},
                                      {"platform", std::string{platform_name()}},
                                      {"defaultExportDirectory",
                                       context.session->default_export_directory().string()}});
}

auto preview_binding(const char* id, const char* request, void* argument) -> void {
    auto& context = *static_cast<Context*>(argument);
    const auto source = first_string(request);
    if (!source) {
        fail_request(context, id, "preview expects one source string");
        return;
    }
    std::optional<Context::PreviewJob> superseded;
    {
        std::lock_guard lock{context.preview_mutex};
        superseded = std::move(context.pending_preview);
        context.pending_preview = Context::PreviewJob{id, std::move(*source)};
    }
    if (superseded)
        respond(context, superseded->id.c_str(),
                icad::json::Value::Object{{"success", false}, {"stale", true}});
    context.preview_ready.notify_one();
}

auto save_binding(const char* id, const char* request, void* argument) -> void {
    auto& context = *static_cast<Context*>(argument);
    const auto source = first_string(request);
    if (!source) {
        fail_request(context, id, "save expects one source string");
        return;
    }
    const auto saved = context.session->save(*source);
    respond(context, id,
            icad::json::Value::Object{{"success", saved.success}, {"message", saved.message}});
}

auto export_binding(const char* id, const char* request, void* argument) -> void {
    auto& context = *static_cast<Context*>(argument);
    auto arguments = string_arguments(request, 2);
    if (!arguments) {
        fail_request(context, id, "export expects source and output-directory strings");
        return;
    }
    if (context.export_worker.joinable())
        context.export_worker.join();
    context.export_worker = std::jthread(
        [&context, reply_id = std::string{id}, source = std::move((*arguments)[0]),
         directory = std::filesystem::path{std::move((*arguments)[1])}](std::stop_token) {
            const auto package = context.session->export_package(source, directory);
            dispatch_reply(context, reply_id, package_value(package));
        });
}

auto style_native_title_bar(webview_t window) -> void {
#ifdef __APPLE__
    auto* native_window = static_cast<id>(webview_get_window(window));
    if (native_window == nil)
        return;
    const auto style_mask = reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(
        native_window, sel_registerName("styleMask"));
    constexpr unsigned long full_size_content_view = 1UL << 15U;
    reinterpret_cast<void (*)(id, SEL, unsigned long)>(objc_msgSend)(
        native_window, sel_registerName("setStyleMask:"), style_mask | full_size_content_view);
    reinterpret_cast<void (*)(id, SEL, signed char)>(objc_msgSend)(
        native_window, sel_registerName("setTitlebarAppearsTransparent:"), 1);
    reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(
        native_window, sel_registerName("setTitleVisibility:"), 1);
#else
    static_cast<void>(window);
#endif
}

[[nodiscard]] auto run_html(const std::filesystem::path& document) -> int {
    const auto window = webview_create(1, nullptr);
    if (window == nullptr) {
        std::cerr << "icad-viewer: could not initialize the platform webview\n";
        return 1;
    }
    const auto title = "ICAD Viewer - " + document.stem().string();
    const auto url = file_url(document);
    const bool configured = webview_set_title(window, title.c_str()) == WEBVIEW_ERROR_OK &&
                            webview_set_size(window, 1280, 800, WEBVIEW_HINT_NONE) ==
                                WEBVIEW_ERROR_OK &&
                            webview_navigate(window, url.c_str()) == WEBVIEW_ERROR_OK;
    if (!configured) {
        std::cerr << "icad-viewer: failed to configure the viewer window\n";
        webview_destroy(window);
        return 1;
    }
    const auto run_result = webview_run(window);
    const auto destroy_result = webview_destroy(window);
    return run_result == WEBVIEW_ERROR_OK && destroy_result == WEBVIEW_ERROR_OK ? 0 : 1;
}

[[nodiscard]] auto run_source(const std::filesystem::path& source_path) -> int {
    icad::viewer::LiveSession session{source_path};
    if (!session.ready()) {
        std::cerr << "icad-viewer: " << session.error() << ": " << source_path << '\n';
        return 2;
    }
    const auto window = webview_create(1, nullptr);
    if (window == nullptr) {
        std::cerr << "icad-viewer: could not initialize the platform webview\n";
        return 1;
    }
    Context context;
    context.window = window;
    context.session = &session;
    style_native_title_bar(window);
    start_preview_worker(context);
    const auto title = "ICAD Live - " + source_path.filename().string();
    std::string html{workbench_html};
    const auto script_position = html.find("</head>");
    html.insert(script_position, "<script>" + std::string{icad::viewer::library_source} +
                                     "</script>");
    const bool configured = webview_set_title(window, title.c_str()) == WEBVIEW_ERROR_OK &&
                            webview_set_size(window, 1440, 900, WEBVIEW_HINT_NONE) ==
                                WEBVIEW_ERROR_OK &&
                            webview_bind(window, "icadRead", read_binding, &context) ==
                                WEBVIEW_ERROR_OK &&
                            webview_bind(window, "icadPreview", preview_binding, &context) ==
                                WEBVIEW_ERROR_OK &&
                            webview_bind(window, "icadSave", save_binding, &context) ==
                                WEBVIEW_ERROR_OK &&
                            webview_bind(window, "icadExport", export_binding, &context) ==
                                WEBVIEW_ERROR_OK &&
                            webview_set_html(window, html.c_str()) == WEBVIEW_ERROR_OK;
    if (!configured) {
        std::cerr << "icad-viewer: failed to initialize the live workbench\n";
        stop_workers(context);
        webview_destroy(window);
        return 1;
    }
    const auto run_result = webview_run(window);
    stop_workers(context);
    const auto destroy_result = webview_destroy(window);
    return run_result == WEBVIEW_ERROR_OK && destroy_result == WEBVIEW_ERROR_OK ? 0 : 1;
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 2 || std::string_view{argv[1]} == "--help" ||
        std::string_view{argv[1]} == "-h") {
        usage(argc == 2 ? std::cout : std::cerr);
        return argc == 2 ? 0 : 2;
    }
    const std::filesystem::path document{argv[1]};
    if (!std::filesystem::is_regular_file(document)) {
        std::cerr << "icad-viewer: file does not exist: " << document << '\n';
        return 2;
    }
    if (document.extension() == ".icad")
        return run_source(document);
    if (document.extension() == ".html")
        return run_html(document);
    std::cerr << "icad-viewer: expected an .icad source or compiled .html file: " << document
              << '\n';
    return 2;
}
