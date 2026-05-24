#include "mail_parser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

bool writeFile(const std::filesystem::path& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return true;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }

    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return s.substr(begin, end - begin);
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::string unquote(std::string s) {
    s = trim(s);

    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }

    return s;
}

std::map<std::string, std::string> parseHeaders(const std::string& header_text) {
    std::map<std::string, std::string> headers;

    std::istringstream stream(header_text);
    std::string line;
    std::string current_name;
    std::string current_value;

    auto flush = [&]() {
        if (!current_name.empty()) {
            headers[toLower(current_name)] = trim(current_value);
        }
        current_name.clear();
        current_value.clear();
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            current_value += " " + trim(line);
            continue;
        }

        flush();

        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }

        current_name = line.substr(0, pos);
        current_value = line.substr(pos + 1);
    }

    flush();
    return headers;
}

bool splitHeaderBody(const std::string& raw, std::string& header_text, std::string& body) {
    auto pos = raw.find("\r\n\r\n");
    if (pos != std::string::npos) {
        header_text = raw.substr(0, pos);
        body = raw.substr(pos + 4);
        return true;
    }

    pos = raw.find("\n\n");
    if (pos != std::string::npos) {
        header_text = raw.substr(0, pos);
        body = raw.substr(pos + 2);
        return true;
    }

    return false;
}

struct HeaderValue {
    std::string value;
    std::map<std::string, std::string> params;
};

HeaderValue parseHeaderValue(const std::string& text) {
    HeaderValue result;

    std::vector<std::string> parts;
    std::string current;
    bool in_quote = false;

    for (char c : text) {
        if (c == '"') {
            in_quote = !in_quote;
            current.push_back(c);
            continue;
        }

        if (c == ';' && !in_quote) {
            parts.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(c);
    }

    parts.push_back(current);

    if (!parts.empty()) {
        result.value = toLower(trim(parts[0]));
    }

    for (size_t i = 1; i < parts.size(); ++i) {
        const auto pos = parts[i].find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = toLower(trim(parts[i].substr(0, pos)));
        std::string value = unquote(parts[i].substr(pos + 1));

        result.params[key] = value;
    }

    return result;
}

std::string getHeader(const std::map<std::string, std::string>& headers, const std::string& name) {
    auto it = headers.find(toLower(name));
    if (it == headers.end()) {
        return {};
    }
    return it->second;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

std::string decodeQuotedPrintable(const std::string& input) {
    std::string output;

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '=') {
            output.push_back(input[i]);
            continue;
        }

        if (i + 1 < input.size() && input[i + 1] == '\n') {
            i += 1;
            continue;
        }

        if (i + 2 < input.size() && input[i + 1] == '\r' && input[i + 2] == '\n') {
            i += 2;
            continue;
        }

        if (i + 2 < input.size()) {
            const int hi = hexValue(input[i + 1]);
            const int lo = hexValue(input[i + 2]);

            if (hi >= 0 && lo >= 0) {
                output.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }

        output.push_back(input[i]);
    }

    return output;
}

std::string decodeHeaderQ(const std::string& input) {
    std::string fixed = input;
    std::replace(fixed.begin(), fixed.end(), '_', ' ');
    return decodeQuotedPrintable(fixed);
}

std::string decodeBase64(const std::string& input) {
    static const std::string table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string clean;
    clean.reserve(input.size());

    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '+' || c == '/' || c == '=') {
            clean.push_back(static_cast<char>(c));
        }
    }

    std::string output;
    int val = 0;
    int valb = -8;

    for (unsigned char c : clean) {
        if (c == '=') {
            break;
        }

        const auto pos = table.find(static_cast<char>(c));
        if (pos == std::string::npos) {
            continue;
        }

        val = (val << 6) + static_cast<int>(pos);
        valb += 6;

        if (valb >= 0) {
            output.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return output;
}

std::string decodeTransfer(const std::string& body, const std::string& encoding) {
    const auto enc = toLower(trim(encoding));

    if (enc == "base64") {
        return decodeBase64(body);
    }

    if (enc == "quoted-printable") {
        return decodeQuotedPrintable(body);
    }

    return body;
}

std::string decodeMimeWords(const std::string& input) {
    std::string output;
    size_t pos = 0;

    while (true) {
        const size_t begin = input.find("=?", pos);
        if (begin == std::string::npos) {
            output += input.substr(pos);
            break;
        }

        output += input.substr(pos, begin - pos);

        const size_t charset_end = input.find('?', begin + 2);
        if (charset_end == std::string::npos) {
            output += input.substr(begin);
            break;
        }

        const size_t encoding_end = input.find('?', charset_end + 1);
        if (encoding_end == std::string::npos) {
            output += input.substr(begin);
            break;
        }

        const size_t end = input.find("?=", encoding_end + 1);
        if (end == std::string::npos) {
            output += input.substr(begin);
            break;
        }

        const std::string encoding =
            toLower(input.substr(charset_end + 1, encoding_end - charset_end - 1));
        const std::string encoded = input.substr(encoding_end + 1, end - encoding_end - 1);

        if (encoding == "b") {
            output += decodeBase64(encoded);
        } else if (encoding == "q") {
            output += decodeHeaderQ(encoded);
        } else {
            output += encoded;
        }

        pos = end + 2;
    }

    return output;
}

std::string sanitizeFilename(std::string filename) {
    filename = decodeMimeWords(filename);

    if (filename.empty()) {
        return "attachment.bin";
    }

    for (char& c : filename) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            c = '_';
        }
    }

    return filename;
}

std::filesystem::path makeUniquePath(const std::filesystem::path& dir,
                                     const std::string& filename) {
    std::filesystem::path path = dir / filename;

    if (!std::filesystem::exists(path)) {
        return path;
    }

    const auto stem = path.stem().string();
    const auto ext = path.extension().string();

    for (int i = 1; i < 10000; ++i) {
        auto candidate = dir / (stem + "_" + std::to_string(i) + ext);
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return path;
}

std::vector<std::string> splitMultipartBody(const std::string& body, const std::string& boundary) {
    std::vector<std::string> parts;

    const std::string marker = "--" + boundary;
    size_t pos = 0;

    while (true) {
        const size_t marker_pos = body.find(marker, pos);
        if (marker_pos == std::string::npos) {
            break;
        }

        const size_t marker_line_end = body.find('\n', marker_pos);
        if (marker_line_end == std::string::npos) {
            break;
        }

        const std::string marker_line = body.substr(marker_pos, marker_line_end - marker_pos);
        if (marker_line.find(marker + "--") != std::string::npos) {
            break;
        }

        const size_t part_begin = marker_line_end + 1;
        const size_t next_marker = body.find(marker, part_begin);
        if (next_marker == std::string::npos) {
            break;
        }

        size_t part_end = next_marker;

        while (part_end > part_begin &&
               (body[part_end - 1] == '\n' || body[part_end - 1] == '\r')) {
            --part_end;
        }

        parts.push_back(body.substr(part_begin, part_end - part_begin));
        pos = next_marker;
    }

    return parts;
}

void parsePart(const std::string& raw, MailParser::MailContent& content,
               const MailParser::Options& options) {
    std::string header_text;
    std::string body;

    if (!splitHeaderBody(raw, header_text, body)) {
        return;
    }

    const auto headers = parseHeaders(header_text);

    if (content.subject.empty()) {
        content.subject = decodeMimeWords(getHeader(headers, "subject"));
    }

    if (content.from.empty()) {
        content.from = decodeMimeWords(getHeader(headers, "from"));
    }

    if (content.to.empty()) {
        content.to = decodeMimeWords(getHeader(headers, "to"));
    }

    auto content_type = parseHeaderValue(getHeader(headers, "content-type"));
    auto disposition = parseHeaderValue(getHeader(headers, "content-disposition"));

    if (content_type.value.empty()) {
        content_type.value = "text/plain";
    }

    const std::string transfer_encoding = getHeader(headers, "content-transfer-encoding");

    if (startsWith(content_type.value, "multipart/")) {
        const auto it = content_type.params.find("boundary");
        if (it == content_type.params.end()) {
            return;
        }

        const auto parts = splitMultipartBody(body, it->second);
        for (const auto& part : parts) {
            parsePart(part, content, options);
        }

        return;
    }

    const std::string decoded_body = decodeTransfer(body, transfer_encoding);

    std::string filename;

    if (auto it = disposition.params.find("filename"); it != disposition.params.end()) {
        filename = it->second;
    } else if (auto it = content_type.params.find("name"); it != content_type.params.end()) {
        filename = it->second;
    }

    const bool is_attachment = disposition.value == "attachment" || !filename.empty() ||
                               content_type.value == "application/pdf";

    if (is_attachment) {
        if (filename.empty()) {
            filename =
                content_type.value == "application/pdf" ? "attachment.pdf" : "attachment.bin";
        }

        filename = sanitizeFilename(filename);

        MailParser::Attachment attachment;
        attachment.filename = filename;
        attachment.content_type = content_type.value;
        attachment.size = decoded_body.size();

        if (options.save_attachments) {
            std::error_code ec;
            std::filesystem::create_directories(options.attachment_save_dir, ec);

            if (!ec) {
                const auto path = makeUniquePath(options.attachment_save_dir, filename);
                if (writeFile(path, decoded_body)) {
                    attachment.saved_path = path;
                }
            }
        }

        content.attachments.push_back(attachment);
        return;
    }

    if (content_type.value == "text/plain") {
        if (!content.text_body.empty()) {
            content.text_body += "\n";
        }
        content.text_body += decoded_body;
        return;
    }

    if (content_type.value == "text/html") {
        if (!content.html_body.empty()) {
            content.html_body += "\n";
        }
        content.html_body += decoded_body;
        return;
    }
}

} // namespace

MailParser::MailParser() = default;

MailParser::~MailParser() = default;

MailParser::Result MailParser::parseFile(const std::filesystem::path& eml_path,
                                         const Options& options) {
    Result result;

    const auto raw = readFile(eml_path);
    if (raw.empty()) {
        result.error_message = "failed to read eml file: " + eml_path.string();
        return result;
    }

    parsePart(raw, result.content, options);

    result.success = true;
    return result;
}

MailParser::Result MailParser::parseFile(const std::filesystem::path& eml_path) {
    return parseFile(eml_path, Options{});
}