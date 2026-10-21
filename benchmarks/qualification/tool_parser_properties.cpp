#include "models/qwen3_5/frontend/tool_call_parser.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;
namespace fi = ninfer::models::qwen3_5::frontend;

Json decoded(const std::string& text, std::shared_ptr<const fi::ToolCallOutputContract> contract,
             unsigned seed, unsigned chunk) {
    fi::ToolCallOutputDecoder decoder(std::move(contract), 64);
    std::mt19937 rng(seed);
    std::string content;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto length = std::min(text.size() - offset,
            std::size_t(chunk ? chunk : 1 + rng() % 31));
        if ((rng() % 5) == 0) content += decoder.feed({});
        content += decoder.feed(std::string_view(text).substr(offset, length));
        offset += length;
    }
    auto tail = decoder.finish();
    content += tail.content;
    Json calls = Json::array();
    for (auto& call : tail.tool_calls)
        calls.push_back(Json{{"name", call.name}, {"arguments", call.arguments_json}});
    return Json{{"content", content}, {"calls", calls},
                {"marker_seen", tail.diagnostics.marker_seen},
                {"fallback", int(tail.diagnostics.fallback_reason)}};
}

int main() {
    const std::vector<std::string> definitions = {R"({"type":"function","function":{"name":"configure","parameters":{"type":"object","properties":{"value":{"type":"string"},"count":{"type":"integer"},"enabled":{"type":"boolean"},"data":{"type":"object"}}}}})"};
    const auto contract = fi::build_tool_call_output_contract(definitions, true);
    const std::string call = "<tool_call>\n<function=configure>\n<parameter=value>\n中文🧪\\path\r\nline\n</parameter>\n<parameter=count>\n17\n</parameter>\n<parameter=enabled>\ntrue\n</parameter>\n<parameter=data>\n{\"a\":[1,2]}\n</parameter>\n</function>\n</tool_call>";
    std::vector<std::string> corpus = {"", " ", "中文🧪 plain\r\n  ", call,
        "leading text\n" + call, call + " trailing text", call + "\n" + call,
        "`" + call + "`", "``" + call + "``", "```xml\n" + call + "\n```",
        "~~~~xml\r\n" + call + "\r\n~~~~", "```xml\n" + call + "\n```\n" + call,
        "Literal <tool_call> is a wrapper. 中文🧪\r\n", "<tool_call>", "<<tool_call>",
        "<function_calls><invoke name=\"configure\"><param name=\"value\">中\n文</param></invoke></function_calls>",
        "<tool_call><function name=\"configure\"><parameter name=\"value\">ok</parameter></function></tool_call>",
        "\\`" + call, "中文🧪 <tool_call> only.\n"};
    for (std::size_t cut = 0; cut < call.size(); ++cut) corpus.push_back(call.substr(0, cut));
    std::mt19937 random(20261020);
    const std::string edits = "<>/=\"'`~ \r\nabcdefghijklmnopqrstuvwxyz";
    const auto seeds = corpus;
    for (unsigned i = 0; i < 3500; ++i) {
        std::string value = seeds[random() % seeds.size()];
        for (unsigned j = 0, n = 1 + random() % 4; j < n; ++j) {
            const std::size_t pos = random() % (value.size() + 1);
            if (random() % 2 && pos < value.size()) value.erase(pos, 1);
            else value.insert(pos, 1, edits[random() % edits.size()]);
        }
        corpus.push_back(std::move(value));
    }
    unsigned comparisons = 0;
    Json failures = Json::array();
    Json invalid_inputs = Json::array();
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const auto& input = corpus[i];
        bool valid_utf8 = true;
        try { (void)Json(input).dump(); }
        catch (const Json::type_error&) { valid_utf8 = false; }
        if (!valid_utf8) {
            std::string hex;
            for (unsigned char byte : input) {
                hex += "0123456789abcdef"[byte >> 4];
                hex += "0123456789abcdef"[byte & 15];
            }
            invalid_inputs.push_back(Json{{"case", i}, {"hex", hex}});
            continue; // Production OutputSession repairs these bytes before this parser.
        }
        try {
        Json whole = decoded(input, contract, 0, std::max(1u, unsigned(input.size())));
        // No structured interpretation means every byte must be preserved.
        if (whole["calls"].empty() && whole["content"] != input)
            failures.push_back(Json{{"case", i}, {"kind", "lost_bytes"}, {"input", input}, {"result", whole}});
        for (unsigned chunk : {1u, 2u, 7u, 0u}) {
            const Json streamed = decoded(input, contract, unsigned(i) + 19, chunk);
            ++comparisons;
            if (whole != streamed) {
                failures.push_back(Json{{"case", i}, {"chunk", chunk}, {"kind", "chunk_difference"},
                    {"input", input}, {"whole", whole}, {"streamed", streamed}});
                break;
            }
        }
        } catch (const std::exception& error) {
            failures.push_back(Json{{"case", i}, {"kind", "exception"}, {"input", input}, {"error", error.what()}});
        }
    }
    // A long argument should stay complete, without quadratic chunk-boundary behavior.
    const std::string large = "<tool_call><function=configure><parameter=value>" +
        std::string(65536, 'x') + "</parameter></function></tool_call>";
    const Json full = decoded(large, contract, 0, unsigned(large.size()));
    const Json chunks = decoded(large, contract, 1, 1);
    ++comparisons;
    if (full != chunks || full["calls"].size() != 1 ||
        Json::parse(full["calls"][0]["arguments"].get<std::string>())["value"] != std::string(65536, 'x'))
        failures.push_back(Json{{"kind", "long_argument"}});
    std::cout << Json{{"seed", 20261020}, {"inputs", corpus.size()+1},
                      {"valid_utf8_inputs", corpus.size()+1-invalid_inputs.size()},
                      {"invalid_utf8_inputs", invalid_inputs},
                      {"chunk_comparisons", comparisons}, {"failures", failures}}.dump(2, ' ', false, Json::error_handler_t::replace) << '\n';
    return failures.empty() ? 0 : 1;
}
