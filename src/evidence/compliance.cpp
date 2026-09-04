#include "icad/evidence/compliance.hpp"

#include "icad/document/revision.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace icad::evidence {
namespace {

using Object = json::Value::Object;
using Array = json::Value::Array;

constexpr std::array<std::uint32_t, 64> sha_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

[[nodiscard]] auto rotate_right(std::uint32_t value, unsigned int count) -> std::uint32_t {
    return std::rotr(value, static_cast<int>(count));
}

[[nodiscard]] auto severity_name(Severity severity) -> std::string_view {
    switch (severity) {
    case Severity::information: return "information";
    case Severity::warning: return "warning";
    case Severity::error: return "error";
    }
    return "error";
}

[[nodiscard]] auto object(std::initializer_list<Object::value_type> fields) -> json::Value {
    return json::Value{Object{fields}};
}

[[nodiscard]] auto string_field(const json::Value* value, std::string_view key)
    -> const std::string* {
    const auto* field = value == nullptr ? nullptr : value->find(key);
    return field == nullptr ? nullptr : field->string();
}

[[nodiscard]] auto array_field(const json::Value* value, std::string_view key) -> const Array* {
    const auto* field = value == nullptr ? nullptr : value->find(key);
    return field == nullptr ? nullptr : field->array();
}

[[nodiscard]] auto object_field(const json::Value* value, std::string_view key) -> const Object* {
    const auto* field = value == nullptr ? nullptr : value->find(key);
    return field == nullptr ? nullptr : field->object();
}

[[nodiscard]] auto allowed(std::string_view value,
                           std::initializer_list<std::string_view> choices) -> bool {
    return std::ranges::find(choices, value) != choices.end();
}

[[nodiscard]] auto is_sha256(std::string_view value) -> bool {
    return value.size() == 64 && std::ranges::all_of(value, [](char character) {
               return std::isdigit(static_cast<unsigned char>(character)) ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] auto location_of(std::string_view source, std::string_view needle)
    -> std::pair<std::size_t, std::size_t> {
    auto offset = source.find(needle);
    if (offset == std::string_view::npos)
        offset = 0;
    const auto line = 1U + static_cast<std::size_t>(
                               std::ranges::count(source.substr(0, offset), '\n'));
    const auto newline = source.substr(0, offset).find_last_of('\n');
    const auto column = newline == std::string_view::npos ? offset + 1 : offset - newline;
    return {line, column};
}

auto add_issue(Evaluation& evaluation, std::string code, Severity severity,
               std::string message, std::string_view manifest_source,
               const std::filesystem::path& manifest_path, std::string_view locator = {}) -> void {
    const auto [line, column] = location_of(manifest_source, locator);
    evaluation.issues.push_back({std::move(code), severity, manifest_path.generic_string(), line,
                                 column, std::move(message)});
}

[[nodiscard]] auto escaped_html(std::string_view value) -> std::string {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output.push_back(character); break;
        }
    }
    return output;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream input{path, std::ios::binary};
    if (!input)
        return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto confined_path(const std::filesystem::path& manifest_path,
                                 std::string_view relative) -> std::optional<std::filesystem::path> {
    const auto supplied = std::filesystem::path{relative};
    if (supplied.empty() || supplied.is_absolute())
        return std::nullopt;
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(
        std::filesystem::absolute(manifest_path.parent_path()), error);
    if (error)
        return std::nullopt;
    const auto resolved = std::filesystem::weakly_canonical(root / supplied, error);
    if (error)
        return std::nullopt;
    const auto within = resolved.lexically_relative(root);
    if (within.empty())
        return resolved;
    const auto first = within.begin();
    if (first == within.end() || *first == std::filesystem::path{".."})
        return std::nullopt;
    return resolved;
}

[[nodiscard]] auto person_complete(const Object* person) -> bool {
    if (person == nullptr)
        return false;
    const json::Value value{*person};
    const auto* name = string_field(&value, "name");
    const auto* organization = string_field(&value, "organization");
    return name != nullptr && !name->empty() && organization != nullptr && !organization->empty();
}

[[nodiscard]] auto string_array(const json::Value* value, std::string_view key)
    -> std::vector<std::string> {
    std::vector<std::string> values;
    const auto* array = array_field(value, key);
    if (array == nullptr)
        return values;
    for (const auto& entry : *array) {
        if (const auto* text = entry.string(); text != nullptr)
            values.push_back(*text);
    }
    return values;
}

[[nodiscard]] auto validate_analysis_result(const json::Value& result,
                                            std::string_view artifact_id,
                                            std::string_view expected_revision,
                                            std::string_view expected_model_sha,
                                            std::string_view expected_units,
                                            std::string_view expected_disposition,
                                            const std::unordered_map<std::string, std::string>&
                                                expected_input_digests,
                                            std::string& reason) -> bool {
    const auto* schema = string_field(&result, "schema");
    const auto* analysis_id = string_field(&result, "analysisId");
    const auto* discipline = string_field(&result, "discipline");
    const auto* revision = string_field(&result, "inputModelRevision");
    const auto* digest = string_field(&result, "inputModelSha256");
    const auto* units = string_field(&result, "units");
    const auto* disposition = string_field(&result, "disposition");
    const auto* solver = object_field(&result, "solver");
    const auto* author = object_field(&result, "author");
    const auto* reviewer = object_field(&result, "independentReviewer");
    if (schema == nullptr || *schema != "icad.analysis.result.v1" || analysis_id == nullptr ||
        *analysis_id != artifact_id || discipline == nullptr || discipline->empty() ||
        solver == nullptr || revision == nullptr || digest == nullptr || units == nullptr ||
        disposition == nullptr || array_field(&result, "assumptions") == nullptr ||
        object_field(&result, "results") == nullptr || object_field(&result, "margins") == nullptr ||
        array_field(&result, "limitations") == nullptr || array_field(&result, "artifacts") == nullptr ||
        !person_complete(author) || !person_complete(reviewer)) {
        reason = "analysis result is missing required icad.analysis.result.v1 fields";
        return false;
    }
    const json::Value author_value{*author};
    const json::Value reviewer_value{*reviewer};
    const auto* author_name = string_field(&author_value, "name");
    const auto* reviewer_name = string_field(&reviewer_value, "name");
    const auto* approved_at = string_field(&reviewer_value, "approvedAt");
    if (approved_at == nullptr || approved_at->empty() ||
        (author_name != nullptr && reviewer_name != nullptr && *author_name == *reviewer_name)) {
        reason = "analysis result lacks a distinct, dated independent review";
        return false;
    }
    const json::Value solver_value{*solver};
    if (string_field(&solver_value, "name") == nullptr ||
        string_field(&solver_value, "version") == nullptr) {
        reason = "analysis result solver name/version is incomplete";
        return false;
    }
    if (*revision != expected_revision || *digest != expected_model_sha) {
        reason = "analysis result was produced from a different model revision";
        return false;
    }
    if (!expected_units.empty() && *units != expected_units) {
        reason = "analysis result units do not match the manifest";
        return false;
    }
    const auto* input_digests = object_field(&result, "inputDigests");
    if (!expected_input_digests.empty() && input_digests == nullptr) {
        reason = "analysis result does not identify its controlled input digests";
        return false;
    }
    if (input_digests != nullptr) {
        for (const auto& [input_id, expected_digest] : expected_input_digests) {
            const json::Value digest_values{*input_digests};
            const auto* recorded_digest = string_field(&digest_values, input_id);
            if (recorded_digest == nullptr || *recorded_digest != expected_digest) {
                reason = "analysis result is stale for controlled input '" + input_id + "'";
                return false;
            }
        }
    }
    if (!allowed(*disposition, {"draft", "in-review", "accepted", "rejected"})) {
        reason = "analysis result disposition is invalid";
        return false;
    }
    if (*disposition != expected_disposition) {
        reason = "analysis result disposition does not match the manifest";
        return false;
    }
    return true;
}

[[nodiscard]] auto issue_json(const Issue& issue) -> json::Value {
    return object({{"code", issue.code},
                   {"severity", std::string{severity_name(issue.severity)}},
                   {"path", issue.path},
                   {"line", static_cast<double>(issue.line)},
                   {"column", static_cast<double>(issue.column)},
                   {"message", issue.message}});
}

} // namespace

auto sha256(std::string_view bytes) -> std::string {
    std::vector<std::uint8_t> message(bytes.begin(), bytes.end());
    const auto bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U)
        message.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));

    std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                       0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                       0x1f83d9abU, 0x5be0cd19U};
    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto offset = chunk + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[offset]) << 24U) |
                           (static_cast<std::uint32_t>(message[offset + 1]) << 16U) |
                           (static_cast<std::uint32_t>(message[offset + 2]) << 8U) |
                           static_cast<std::uint32_t>(message[offset + 3]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const auto s0 = rotate_right(words[index - 15U], 7U) ^
                            rotate_right(words[index - 15U], 18U) ^
                            (words[index - 15U] >> 3U);
            const auto s1 = rotate_right(words[index - 2U], 17U) ^
                            rotate_right(words[index - 2U], 19U) ^
                            (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }
        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choice + sha_constants[index] + words[index];
            const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : state)
        output << std::setw(8) << value;
    return output.str();
}

auto sha256_file(const std::filesystem::path& path) -> std::string {
    const auto source = read_file(path);
    return source ? sha256(*source) : std::string{};
}

auto evaluate(std::string_view model_source, const compiler::ir::Project& project,
              std::string_view manifest_source, const std::filesystem::path& manifest_path,
              std::string_view requested_basis) -> Evaluation {
    Evaluation evaluation;
    evaluation.model_revision = document::revision_id(model_source);
    evaluation.model_sha256 = sha256(model_source);
    auto parsed = json::parse(manifest_source);
    if (!parsed.ok() || parsed.value->object() == nullptr) {
        const auto [line, column] = location_of(manifest_source, manifest_source.substr(
            std::min(parsed.offset, manifest_source.size()), 1));
        evaluation.issues.push_back({"ICAD-E0001", Severity::error,
                                     manifest_path.generic_string(), line, column,
                                     "manifest JSON is invalid: " + parsed.error});
        Array parse_issues{issue_json(evaluation.issues.front())};
        evaluation.compliance = object({{"schema", "icad.compliance.v1"},
                                        {"basis", ""},
                                        {"modelRevision", evaluation.model_revision},
                                        {"modelSha256", evaluation.model_sha256},
                                        {"lifecycleState", ""},
                                        {"manifestValid", false},
                                        {"releaseReady", false},
                                        {"openRequirements", 0.0},
                                        {"safetyCriticalRequirements", 0.0},
                                        {"validationRequirementsOpen", 0.0},
                                        {"openApplicableCompliance", 0.0},
                                        {"blockingHazards", 0.0},
                                        {"requirements", json::Value{Array{}}},
                                        {"compliance", json::Value{Array{}}},
                                        {"artifacts", json::Value{Array{}}},
                                        {"hazards", json::Value{Array{}}},
                                        {"prohibitedClaims", json::Value{Array{}}},
                                        {"issues", json::Value{std::move(parse_issues)}}});
        return evaluation;
    }
    evaluation.normalized_manifest = *parsed.value;
    const auto& root = *parsed.value;
    const auto* schema = string_field(&root, "schema");
    const auto* project_name = string_field(&root, "project");
    const auto* basis = string_field(&root, "basis");
    const auto* lifecycle = string_field(&root, "lifecycleState");
    evaluation.basis = basis == nullptr ? std::string{} : *basis;
    evaluation.lifecycle_state = lifecycle == nullptr ? std::string{} : *lifecycle;
    if (schema == nullptr || *schema != "icad.evidence.manifest.v1")
        add_issue(evaluation, "ICAD-E0002", Severity::error,
                  "schema must be icad.evidence.manifest.v1", manifest_source, manifest_path,
                  "\"schema\"");
    if (project_name == nullptr || *project_name != project.name)
        add_issue(evaluation, "ICAD-E0003", Severity::error,
                  "manifest project must match the compiled ICAD project", manifest_source,
                  manifest_path, "\"project\"");
    if (basis == nullptr || basis->empty())
        add_issue(evaluation, "ICAD-E0004", Severity::error, "certification basis is required",
                  manifest_source, manifest_path, "\"basis\"");
    else if (!requested_basis.empty() && *basis != requested_basis)
        add_issue(evaluation, "ICAD-E0005", Severity::error,
                  "requested certification basis does not match the manifest", manifest_source,
                  manifest_path, "\"basis\"");
    if (lifecycle == nullptr ||
        !allowed(*lifecycle, {"BENCHMARK", "DEVELOPMENT", "GROUND_TEST_RELEASED",
                             "DEMONSTRATOR_VALIDATED"})) {
        add_issue(evaluation, "ICAD-E0006", Severity::error,
                  lifecycle != nullptr && *lifecycle == "TYPE_CERTIFIED"
                      ? "TYPE_CERTIFIED cannot be self-declared; authority certificate evidence is required outside this program"
                      : "unsupported lifecycleState",
                  manifest_source, manifest_path, "\"lifecycleState\"");
    }

    const auto* model = root.find("model");
    const auto* manifest_revision = string_field(model, "revision");
    const auto* manifest_sha = string_field(model, "sha256");
    if (model == nullptr || model->object() == nullptr || manifest_revision == nullptr ||
        manifest_sha == nullptr || !is_sha256(*manifest_sha)) {
        add_issue(evaluation, "ICAD-E0010", Severity::error,
                  "model revision and lowercase SHA-256 are required", manifest_source,
                  manifest_path, "\"model\"");
    } else {
        if (*manifest_revision != evaluation.model_revision)
            add_issue(evaluation, "ICAD-E0011", Severity::error,
                      "manifest model revision is stale", manifest_source, manifest_path,
                      *manifest_revision);
        if (*manifest_sha != evaluation.model_sha256)
            add_issue(evaluation, "ICAD-E0012", Severity::error,
                      "manifest model SHA-256 is stale", manifest_source, manifest_path,
                      *manifest_sha);
    }

    std::unordered_set<std::string> entities;
    for (const auto& parameter : project.parameters) entities.insert(parameter.name);
    for (const auto& material : project.materials) entities.insert(material.name);
    for (const auto& body : project.bodies) entities.insert(body.name);
    for (const auto& interface : project.interfaces) entities.insert(interface.name);
    for (const auto& connection : project.connections) entities.insert(connection.name);
    for (const auto& joint : project.joints) entities.insert(joint.name);

    std::unordered_map<std::string, std::string> controlled_input_digests;
    const auto* controlled_inputs = array_field(&root, "controlledInputs");
    if (controlled_inputs == nullptr) {
        add_issue(evaluation, "ICAD-E0013", Severity::error,
                  "controlledInputs array is required", manifest_source, manifest_path,
                  "\"controlledInputs\"");
    } else {
        std::unordered_set<std::string> ids;
        for (const auto& input : *controlled_inputs) {
            const auto* id = string_field(&input, "id");
            const auto* kind = string_field(&input, "kind");
            const auto* path = string_field(&input, "path");
            const auto* digest = string_field(&input, "sha256");
            const std::string locator = id == nullptr ? "\"controlledInputs\"" : *id;
            if (id == nullptr || id->empty() || !ids.insert(*id).second || kind == nullptr ||
                !allowed(*kind, {"material-definition", "load-case", "analysis-input",
                                 "test-definition", "facility-requirement"}) ||
                path == nullptr || digest == nullptr || !is_sha256(*digest)) {
                add_issue(evaluation, "ICAD-E0014", Severity::error,
                          "controlled input identity, kind, path, and lowercase SHA-256 are required and IDs must be unique",
                          manifest_source, manifest_path, locator);
                continue;
            }
            const auto resolved = confined_path(manifest_path, *path);
            if (!resolved) {
                add_issue(evaluation, "ICAD-E0015", Severity::error,
                          "controlled input path must remain inside the manifest directory",
                          manifest_source, manifest_path, locator);
                continue;
            }
            const auto content = read_file(*resolved);
            if (!content) {
                add_issue(evaluation, "ICAD-E0016", Severity::error,
                          "controlled input file is missing", manifest_source, manifest_path,
                          locator);
                continue;
            }
            const auto actual_digest = sha256(*content);
            if (actual_digest != *digest) {
                add_issue(evaluation, "ICAD-E0017", Severity::error,
                          "controlled input SHA-256 is stale", manifest_source, manifest_path,
                          locator);
            }
            controlled_input_digests.emplace(*id, actual_digest);
        }
    }

    std::unordered_map<std::string, bool> artifacts_accepted;
    const auto* artifacts = array_field(&root, "artifacts");
    if (artifacts == nullptr) {
        add_issue(evaluation, "ICAD-E0020", Severity::error, "artifacts array is required",
                  manifest_source, manifest_path, "\"artifacts\"");
    } else {
        std::unordered_set<std::string> ids;
        for (const auto& artifact : *artifacts) {
            const auto* id = string_field(&artifact, "id");
            const auto* kind = string_field(&artifact, "kind");
            const auto* path = string_field(&artifact, "path");
            const auto* digest = string_field(&artifact, "sha256");
            const auto* disposition = string_field(&artifact, "disposition");
            const auto* input_revision = string_field(&artifact, "inputModelRevision");
            const auto* input_sha = string_field(&artifact, "inputModelSha256");
            const auto* input_digests = object_field(&artifact, "inputDigests");
            const auto* expected_units = string_field(&artifact, "units");
            const auto* author = object_field(&artifact, "author");
            const auto* reviewer = object_field(&artifact, "independentReviewer");
            const std::string locator = id == nullptr ? "\"artifacts\"" : *id;
            if (id == nullptr || id->empty() || !ids.insert(*id).second || kind == nullptr ||
                !allowed(*kind, {"analysis", "test", "approval", "drawing", "manufacturing",
                                 "calibration"}) ||
                path == nullptr || digest == nullptr || disposition == nullptr ||
                input_revision == nullptr || input_sha == nullptr || !person_complete(author) ||
                !person_complete(reviewer) ||
                ((*kind == "analysis" || *kind == "test") && expected_units == nullptr)) {
                add_issue(evaluation, "ICAD-E0021", Severity::error,
                          "artifact identity, kind, path, digest, disposition, and model inputs are required and IDs must be unique",
                          manifest_source, manifest_path, locator);
                continue;
            }
            bool accepted = *disposition == "accepted";
            if (!allowed(*disposition, {"draft", "in-review", "accepted", "rejected"})) {
                add_issue(evaluation, "ICAD-E0022", Severity::error,
                          "artifact disposition is invalid", manifest_source, manifest_path,
                          locator);
                accepted = false;
            }
            if (*input_revision != evaluation.model_revision ||
                *input_sha != evaluation.model_sha256) {
                add_issue(evaluation, "ICAD-E0023", Severity::error,
                          "artifact evidence is stale for the current model", manifest_source,
                          manifest_path, locator);
                accepted = false;
            }
            if (!controlled_input_digests.empty() && input_digests == nullptr) {
                add_issue(evaluation, "ICAD-E0029", Severity::error,
                          "artifact is stale because controlled input digests are missing",
                          manifest_source, manifest_path, locator);
                accepted = false;
            } else if (input_digests != nullptr) {
                const json::Value input_digest_values{*input_digests};
                for (const auto& [input_id, expected_digest] : controlled_input_digests) {
                    const auto* recorded_digest = string_field(&input_digest_values, input_id);
                    if (recorded_digest == nullptr || *recorded_digest != expected_digest) {
                        add_issue(evaluation, "ICAD-E0029", Severity::error,
                                  "artifact is stale for controlled input '" + input_id + "'",
                                  manifest_source, manifest_path, locator);
                        accepted = false;
                    }
                }
            }
            const auto resolved = confined_path(manifest_path, *path);
            if (!resolved) {
                add_issue(evaluation, "ICAD-E0024", Severity::error,
                          "artifact path must remain inside the manifest directory", manifest_source,
                          manifest_path, locator);
                accepted = false;
            } else {
                const auto content = read_file(*resolved);
                if (!content) {
                    add_issue(evaluation, "ICAD-E0025", Severity::error,
                              "artifact file is missing", manifest_source, manifest_path, locator);
                    accepted = false;
                } else if (!is_sha256(*digest) || sha256(*content) != *digest) {
                    add_issue(evaluation, "ICAD-E0026", Severity::error,
                              "artifact SHA-256 does not match its file", manifest_source,
                              manifest_path, locator);
                    accepted = false;
                } else if (*kind == "analysis") {
                    const auto analysis = json::parse(*content);
                    std::string reason;
                    if (!analysis.ok() ||
                        !validate_analysis_result(*analysis.value, *id, evaluation.model_revision,
                                                  evaluation.model_sha256,
                                                  expected_units == nullptr ? std::string_view{}
                                                                            : std::string_view{*expected_units},
                                                  *disposition,
                                                  controlled_input_digests,
                                                  reason)) {
                        add_issue(evaluation, "ICAD-E0027", Severity::error,
                                  reason.empty() ? "analysis artifact is invalid JSON" : reason,
                                  manifest_source, manifest_path, locator);
                        accepted = false;
                    }
                }
            }
            if (accepted) {
                const json::Value reviewer_value{reviewer == nullptr ? Object{} : *reviewer};
                const auto* approved_at = string_field(&reviewer_value, "approvedAt");
                const json::Value author_value{author == nullptr ? Object{} : *author};
                const auto* author_name = string_field(&author_value, "name");
                const auto* reviewer_name = string_field(&reviewer_value, "name");
                if (!person_complete(author) || !person_complete(reviewer) ||
                    approved_at == nullptr || approved_at->empty() ||
                    (author_name != nullptr && reviewer_name != nullptr &&
                     *author_name == *reviewer_name)) {
                    add_issue(evaluation, "ICAD-E0028", Severity::error,
                              "accepted evidence requires a distinct, complete independent approval",
                              manifest_source, manifest_path, locator);
                    accepted = false;
                }
            }
            artifacts_accepted[*id] = accepted;
        }
    }

    std::size_t open_requirements = 0;
    std::size_t safety_requirements = 0;
    std::size_t validation_requirements_open = 0;
    const auto* requirements = array_field(&root, "requirements");
    if (requirements == nullptr || requirements->empty()) {
        add_issue(evaluation, "ICAD-E0030", Severity::error,
                  "at least one traceable requirement is required", manifest_source,
                  manifest_path, "\"requirements\"");
    } else {
        std::unordered_set<std::string> ids;
        for (const auto& requirement : *requirements) {
            const auto* id = string_field(&requirement, "id");
            const auto* title = string_field(&requirement, "title");
            const auto* state = string_field(&requirement, "evidenceState");
            const auto* criticality = string_field(&requirement, "criticality");
            const std::string locator = id == nullptr ? "\"requirements\"" : *id;
            bool supported = false;
            if (id == nullptr || id->empty() || !ids.insert(*id).second || title == nullptr ||
                title->empty() || state == nullptr || criticality == nullptr ||
                array_field(&requirement, "entities") == nullptr ||
                array_field(&requirement, "evidence") == nullptr ||
                !allowed(*state, {"given", "assumed", "calculated", "independently-reviewed",
                                  "test-demonstrated"}) ||
                !allowed(*criticality, {"ordinary", "important", "safety-critical"})) {
                add_issue(evaluation, "ICAD-E0031", Severity::error,
                          "requirement fields or enumerations are invalid", manifest_source,
                          manifest_path, locator);
                ++open_requirements;
                continue;
            }
            for (const auto& entity : string_array(&requirement, "entities")) {
                if (!entities.contains(entity))
                    add_issue(evaluation, "ICAD-E0032", Severity::error,
                              "requirement references unknown ICAD entity '" + entity + "'",
                              manifest_source, manifest_path, locator);
            }
            const auto evidence_ids = string_array(&requirement, "evidence");
            supported = !evidence_ids.empty() && std::ranges::all_of(evidence_ids, [&](const auto& evidence_id) {
                const auto found = artifacts_accepted.find(evidence_id);
                return found != artifacts_accepted.end() && found->second;
            });
            if (*criticality == "safety-critical") {
                ++safety_requirements;
                if (*state != "test-demonstrated")
                    ++validation_requirements_open;
                if ((!allowed(*state, {"independently-reviewed", "test-demonstrated"}) ||
                     !supported) &&
                    lifecycle != nullptr && *lifecycle != "DEVELOPMENT" && *lifecycle != "BENCHMARK") {
                    add_issue(evaluation, "ICAD-E0033", Severity::error,
                              "safety-critical requirement lacks accepted reviewed evidence",
                              manifest_source, manifest_path, locator);
                }
            }
            if (!supported)
                ++open_requirements;
        }
    }

    std::size_t open_compliance = 0;
    const auto* compliance_rows = array_field(&root, "compliance");
    if (compliance_rows == nullptr || compliance_rows->empty()) {
        add_issue(evaluation, "ICAD-E0040", Severity::error,
                  "CS-E applicability matrix is required", manifest_source, manifest_path,
                  "\"compliance\"");
    } else {
        std::unordered_set<std::string> paragraphs;
        for (const auto& row : *compliance_rows) {
            const auto* paragraph = string_field(&row, "paragraph");
            const auto* applicability = string_field(&row, "applicability");
            const auto* rationale = string_field(&row, "rationale");
            const auto* reviewer = string_field(&row, "reviewer");
            const auto* status = string_field(&row, "status");
            const std::string locator = paragraph == nullptr ? "\"compliance\"" : *paragraph;
            if (paragraph == nullptr || paragraph->empty() ||
                !paragraphs.insert(*paragraph).second || applicability == nullptr ||
                rationale == nullptr || rationale->empty() || reviewer == nullptr ||
                reviewer->empty() || status == nullptr || array_field(&row, "evidence") == nullptr ||
                !allowed(*applicability, {"applicable", "development-analogue",
                                         "deferred-to-flight-program", "not-applicable"}) ||
                !allowed(*status, {"open", "partially-supported", "satisfied"})) {
                add_issue(evaluation, "ICAD-E0041", Severity::error,
                          "compliance row is incomplete or has an invalid status", manifest_source,
                          manifest_path, locator);
                ++open_compliance;
                continue;
            }
            const auto evidence_ids = string_array(&row, "evidence");
            const bool supported = !evidence_ids.empty() &&
                                   std::ranges::all_of(evidence_ids, [&](const auto& evidence_id) {
                                       const auto found = artifacts_accepted.find(evidence_id);
                                       return found != artifacts_accepted.end() && found->second;
                                   });
            if (*status == "satisfied" && !supported) {
                add_issue(evaluation, "ICAD-E0042", Severity::error,
                          "satisfied compliance row lacks accepted evidence",
                          manifest_source, manifest_path, locator);
                ++open_compliance;
                continue;
            }
            if ((*applicability == "applicable" || *applicability == "development-analogue") &&
                *status != "satisfied")
                ++open_compliance;
        }
    }

    std::size_t blocking_hazards = 0;
    if (const auto* hazards = array_field(&root, "hazards"); hazards != nullptr) {
        std::unordered_set<std::string> ids;
        for (const auto& hazard : *hazards) {
            const auto* id = string_field(&hazard, "id");
            const auto* title = string_field(&hazard, "title");
            const auto* severity = string_field(&hazard, "severity");
            const auto* status = string_field(&hazard, "status");
            const std::string locator = id == nullptr ? "\"hazards\"" : *id;
            if (id == nullptr || id->empty() || !ids.insert(*id).second || title == nullptr ||
                title->empty() || severity == nullptr || status == nullptr ||
                !allowed(*severity, {"catastrophic", "critical", "major", "minor"}) ||
                !allowed(*status, {"open", "mitigated", "closed"})) {
                add_issue(evaluation, "ICAD-E0045", Severity::error,
                          "hazard record is incomplete or has an invalid enumeration",
                          manifest_source, manifest_path, locator);
                continue;
            }
            if (severity != nullptr && status != nullptr &&
                (*severity == "catastrophic" || *severity == "critical") && *status != "closed")
                ++blocking_hazards;
        }
    } else
        add_issue(evaluation, "ICAD-E0045", Severity::error, "hazards array is required",
                  manifest_source, manifest_path, "\"hazards\"");

    const auto approvals = string_array(&root, "approvals");
    const auto* approval_values = array_field(&root, "approvals");
    if (approval_values == nullptr || approvals.size() != approval_values->size() ||
        std::unordered_set<std::string>(approvals.begin(), approvals.end()).size() != approvals.size())
        add_issue(evaluation, "ICAD-E0046", Severity::error,
                  "approvals must be an array of unique strings", manifest_source, manifest_path,
                  "\"approvals\"");
    const auto prohibited_claims = string_array(&root, "prohibitedClaims");
    for (const auto claim : {"EASA certified", "airworthy", "flight approved", "TYPE_CERTIFIED"}) {
        if (std::ranges::find(prohibited_claims, claim) == prohibited_claims.end())
            add_issue(evaluation, "ICAD-E0047", Severity::error,
                      "required prohibited claim is missing: " + std::string{claim},
                      manifest_source, manifest_path, "\"prohibitedClaims\"");
    }
    const auto has_approval = [&](std::string_view approval) {
        return std::ranges::find(approvals, approval) != approvals.end();
    };
    const bool release_requested = lifecycle != nullptr &&
                                   (*lifecycle == "GROUND_TEST_RELEASED" ||
                                    *lifecycle == "DEMONSTRATOR_VALIDATED");
    if (release_requested) {
        for (const auto required : {"test-readiness-review", "morocco-facility-permits",
                                    "independent-safety-review", "manufacturing-conformity-release",
                                    "test-director-release"}) {
            if (!has_approval(required))
                add_issue(evaluation, "ICAD-E0050", Severity::error,
                          "ground-test release approval is missing: " + std::string{required},
                          manifest_source, manifest_path, "\"approvals\"");
        }
        if (blocking_hazards != 0)
            add_issue(evaluation, "ICAD-E0051", Severity::error,
                      "open catastrophic or critical hazards block ground-test release",
                      manifest_source, manifest_path, "\"hazards\"");
        if (open_compliance != 0 || open_requirements != 0)
            add_issue(evaluation, "ICAD-E0052", Severity::error,
                      "open requirements or applicable compliance rows block release",
                      manifest_source, manifest_path, "\"lifecycleState\"");
    }
    if (lifecycle != nullptr && *lifecycle == "DEMONSTRATOR_VALIDATED") {
        for (const auto required : {"shutdown-and-protection-test",
                                    "operating-envelope-demonstration",
                                    "endurance-and-teardown-acceptance",
                                    "final-ground-demonstrator-report"}) {
            if (!has_approval(required))
                add_issue(evaluation, "ICAD-E0053", Severity::error,
                          "demonstrator validation approval is missing: " + std::string{required},
                          manifest_source, manifest_path, "\"approvals\"");
        }
        if (validation_requirements_open != 0)
            add_issue(evaluation, "ICAD-E0054", Severity::error,
                      "safety-critical requirements are not all test-demonstrated",
                      manifest_source, manifest_path, "\"requirements\"");
    }

    evaluation.manifest_valid = std::ranges::none_of(evaluation.issues, [](const Issue& issue) {
        return issue.severity == Severity::error;
    });
    evaluation.release_ready = evaluation.manifest_valid && release_requested;

    Array issues_json;
    for (const auto& issue : evaluation.issues)
        issues_json.push_back(issue_json(issue));
    Array prohibited;
    for (const auto claim : {"EASA certified", "airworthy", "flight approved", "TYPE_CERTIFIED"})
        prohibited.emplace_back(claim);
    evaluation.compliance = object({
        {"schema", "icad.compliance.v1"},
        {"basis", evaluation.basis},
        {"modelRevision", evaluation.model_revision},
        {"modelSha256", evaluation.model_sha256},
        {"lifecycleState", evaluation.lifecycle_state},
        {"manifestValid", evaluation.manifest_valid},
        {"releaseReady", evaluation.release_ready},
        {"openRequirements", static_cast<double>(open_requirements)},
        {"safetyCriticalRequirements", static_cast<double>(safety_requirements)},
        {"validationRequirementsOpen", static_cast<double>(validation_requirements_open)},
        {"openApplicableCompliance", static_cast<double>(open_compliance)},
        {"blockingHazards", static_cast<double>(blocking_hazards)},
        {"requirements", root.find("requirements") == nullptr
                             ? json::Value{Array{}}
                             : *root.find("requirements")},
        {"compliance", root.find("compliance") == nullptr
                           ? json::Value{Array{}}
                           : *root.find("compliance")},
        {"artifacts", root.find("artifacts") == nullptr
                          ? json::Value{Array{}}
                          : *root.find("artifacts")},
        {"hazards", root.find("hazards") == nullptr
                        ? json::Value{Array{}}
                        : *root.find("hazards")},
        {"prohibitedClaims", json::Value{std::move(prohibited)}},
        {"issues", json::Value{std::move(issues_json)}},
    });
    return evaluation;
}

auto evidence_json(const Evaluation& evaluation) -> std::string {
    Object output;
    if (const auto* manifest = evaluation.normalized_manifest.object(); manifest != nullptr)
        output = *manifest;
    output["schema"] = "icad.evidence.manifest.v1";
    output["evaluation"] = evaluation.compliance;
    return json::serialize(json::Value{std::move(output)});
}

auto compliance_json(const Evaluation& evaluation) -> std::string {
    return json::serialize(evaluation.compliance);
}

auto compliance_html(const Evaluation& evaluation) -> std::string {
    std::ostringstream output;
    output << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>ICAD Ground Demonstrator Compliance</title><style>"
              "body{font:15px system-ui;margin:32px;color:#162033;background:#f7f8fa}"
              "main{max-width:1100px;margin:auto;background:white;padding:32px;border:1px solid #ccd3df}"
              "h1{margin-top:0}.ok{color:#087f5b}.blocked{color:#c92a2a}"
              "table{border-collapse:collapse;width:100%}th,td{border:1px solid #ccd3df;padding:8px;text-align:left}"
              "code{font-family:ui-monospace,monospace}footer{margin-top:28px;color:#596579}</style></head><body><main>"
           << "<h1>Turbojet ground-demonstrator evidence report</h1><p><strong>Lifecycle:</strong> "
           << escaped_html(evaluation.lifecycle_state) << "</p><p><strong>Basis:</strong> "
           << escaped_html(evaluation.basis) << "</p><p><strong>Model revision:</strong> <code>"
           << escaped_html(evaluation.model_revision) << "</code></p><p><strong>Model SHA-256:</strong> <code>"
           << escaped_html(evaluation.model_sha256) << "</code></p><h2 class=\""
           << (evaluation.release_ready ? "ok" : "blocked") << "\">"
           << (evaluation.release_ready ? "Release evidence accepted" : "Ground-test release blocked")
           << "</h2><p>This report is development assurance only. It is not an EASA type certificate, "
              "airworthiness approval, flight approval, or authorization to fire an engine.</p>";
    const auto render_manifest_table = [&](std::string_view heading, std::string_view field,
                                           std::initializer_list<std::string_view> columns) {
        output << "<h2>" << escaped_html(heading) << "</h2><table><thead><tr>";
        for (const auto column : columns)
            output << "<th>" << escaped_html(column) << "</th>";
        output << "</tr></thead><tbody>";
        const auto* rows = array_field(&evaluation.normalized_manifest, field);
        if (rows == nullptr || rows->empty())
            output << "<tr><td colspan=\"" << columns.size() << "\">No records.</td></tr>";
        if (rows != nullptr) {
            for (const auto& row : *rows) {
                output << "<tr>";
                for (const auto column : columns) {
                    const auto* value = string_field(&row, column);
                    output << "<td>" << escaped_html(value == nullptr ? std::string_view{}
                                                                       : std::string_view{*value})
                           << "</td>";
                }
                output << "</tr>";
            }
        }
        output << "</tbody></table>";
    };
    render_manifest_table("Requirements", "requirements",
                          {"id", "title", "criticality", "evidenceState"});
    render_manifest_table("CS-E applicability and compliance", "compliance",
                          {"paragraph", "applicability", "status", "reviewer", "rationale"});
    render_manifest_table("Evidence artifacts", "artifacts",
                          {"id", "kind", "disposition", "path"});
    render_manifest_table("Hazards", "hazards", {"id", "severity", "status", "title"});
    output << "<h2>Diagnostics</h2><table><thead><tr><th>Severity</th><th>Code</th><th>Location</th><th>Message</th></tr></thead><tbody>";
    if (evaluation.issues.empty())
        output << "<tr><td colspan=\"4\">No evidence diagnostics.</td></tr>";
    for (const auto& issue : evaluation.issues) {
        output << "<tr><td>" << severity_name(issue.severity) << "</td><td><code>"
               << escaped_html(issue.code) << "</code></td><td>"
               << escaped_html(issue.path) << ':' << issue.line << ':' << issue.column
               << "</td><td>" << escaped_html(issue.message) << "</td></tr>";
    }
    output << "</tbody></table><footer>Generated from icad.compliance.v1 evidence. External approvals and signed records remain authoritative.</footer></main></body></html>";
    return output.str();
}

} // namespace icad::evidence
