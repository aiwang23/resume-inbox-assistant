#include "mail_fetcher.h"

#include <curl/curl.h>
#include <sqlite_orm/sqlite_orm.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

namespace {
    struct MailStateRecord {
        int id = 0;
        std::string account;
        std::string mailbox;
        std::string uid;
        std::string eml_path;
        std::string created_at;
    };

    auto makeMailStateStorage(const std::filesystem::path& db_path) {
        using namespace sqlite_orm;

        return make_storage(
            db_path.string(),
            make_table(
                "mail_state",
                make_column("id", &MailStateRecord::id, primary_key().autoincrement()),
                make_column("account", &MailStateRecord::account),
                make_column("mailbox", &MailStateRecord::mailbox),
                make_column("uid", &MailStateRecord::uid),
                make_column("eml_path", &MailStateRecord::eml_path),
                make_column("created_at", &MailStateRecord::created_at)
            )
        );
    }

    std::string nowText() {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif

        char buffer[32]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        return buffer;
    }

    void initCurlOnce() {
        static std::once_flag flag;
        std::call_once(flag, []() {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        });
    }

    size_t writeToStringCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* output = static_cast<std::string*>(userdata);
        const size_t total_size = size * nmemb;
        output->append(ptr, total_size);
        return total_size;
    }

    bool isAllDigits(const std::string& text) {
        if (text.empty()) {
            return false;
        }

        return std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isdigit(c);
        });
    }

    std::vector<std::string> parseSearchUids(const std::string& response) {
        std::vector<std::string> uids;

        std::istringstream stream(response);
        std::string line;

        while (std::getline(stream, line)) {
            if (line.find("SEARCH") == std::string::npos) {
                continue;
            }

            std::istringstream line_stream(line);
            std::string token;
            bool found_search = false;

            while (line_stream >> token) {
                if (!token.empty() && token.back() == '\r') {
                    token.pop_back();
                }

                if (found_search && isAllDigits(token)) {
                    uids.push_back(token);
                }

                if (token == "SEARCH") {
                    found_search = true;
                }
            }
        }

        return uids;
    }

    bool extractFirstImapLiteral(const std::string& response, std::string& literal) {
        for (size_t i = 0; i < response.size(); ++i) {
            if (response[i] != '{') {
                continue;
            }

            size_t j = i + 1;
            while (j < response.size() && std::isdigit(static_cast<unsigned char>(response[j]))) {
                ++j;
            }

            if (j >= response.size() || response[j] != '}') {
                continue;
            }

            const std::string size_text = response.substr(i + 1, j - i - 1);
            if (size_text.empty()) {
                continue;
            }

            size_t data_start = j + 1;

            if (data_start < response.size() && response[data_start] == '\r') {
                ++data_start;
            }

            if (data_start < response.size() && response[data_start] == '\n') {
                ++data_start;
            }

            const auto literal_size = static_cast<size_t>(std::stoull(size_text));

            if (data_start + literal_size > response.size()) {
                return false;
            }

            literal = response.substr(data_start, literal_size);
            return true;
        }

        return false;
    }

    bool performImapCommand(const MailFetcher::Account& account,
                            const std::string& imap_url,
                            const std::string& command,
                            std::string& response,
                            std::string& error_message) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            error_message = "curl_easy_init failed";
            return false;
        }

        char error_buffer[CURL_ERROR_SIZE]{};

        curl_easy_setopt(curl, CURLOPT_URL, imap_url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERNAME, account.username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, account.password.c_str());

        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, command.c_str());

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        const CURLcode code = curl_easy_perform(curl);

        if (code != CURLE_OK) {
            error_message = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
            curl_easy_cleanup(curl);
            return false;
        }

        curl_easy_cleanup(curl);
        return true;
    }

    template <typename Storage>
    bool isUidProcessed(Storage& storage,
                        const std::string& account,
                        const std::string& mailbox,
                        const std::string& uid) {
        for (const auto& item : storage.template iterate<MailStateRecord>()) {
            if (item.account == account && item.mailbox == mailbox && item.uid == uid) {
                return true;
            }
        }

        return false;
    }

    template <typename Storage>
    void insertMailState(Storage& storage,
                         const std::string& account,
                         const std::string& mailbox,
                         const std::string& uid,
                         const std::filesystem::path& eml_path) {
        MailStateRecord record;
        record.account = account;
        record.mailbox = mailbox;
        record.uid = uid;
        record.eml_path = eml_path.string();
        record.created_at = nowText();

        storage.insert(record);
    }
} // namespace

MailFetcher::MailFetcher() {
    initCurlOnce();
}

MailFetcher::~MailFetcher() = default;

void MailFetcher::setAccount(const Account& account) {
    account_ = account;
}

void MailFetcher::setOptions(const Options& options) {
    options_ = options;
}

std::string MailFetcher::buildImapUrl() const {
    std::ostringstream oss;

    if (account_.use_ssl) {
        oss << "imaps://";
    } else {
        oss << "imap://";
    }

    oss << account_.host << ":" << account_.port << "/" << account_.mailbox;

    return oss.str();
}

MailFetcher::Result MailFetcher::fetchNewMails() {
    Result result;

    if (account_.host.empty() || account_.username.empty() || account_.password.empty()) {
        result.error_message = "mail account is not configured";
        return result;
    }

    if (options_.save_dir.empty()) {
        result.error_message = "save_dir is empty";
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(options_.save_dir, ec);
    if (ec) {
        result.error_message = "failed to create save_dir: " + ec.message();
        return result;
    }

    auto storage = makeMailStateStorage(options_.state_db);
    storage.sync_schema();

    const std::string imap_url = buildImapUrl();

    std::string search_response;
    std::string search_error;

    const std::string search_command =
        options_.fetch_policy == FetchPolicy::UnseenOnly
            ? "UID SEARCH UNSEEN"
            : "UID SEARCH ALL";

    if (!performImapCommand(account_, imap_url, search_command, search_response, search_error)) {
        result.error_message = "search mail failed: " + search_error;
        return result;
    }

    auto uids = parseSearchUids(search_response);

    std::vector<std::string> pending_uids;
    pending_uids.reserve(uids.size());

    for (const auto& uid : uids) {
        if (!isUidProcessed(storage, account_.username, account_.mailbox, uid)) {
            pending_uids.push_back(uid);
        }
    }

    if (options_.max_count > 0 && static_cast<int>(pending_uids.size()) > options_.max_count) {
        pending_uids.erase(
            pending_uids.begin(),
            pending_uids.end() - options_.max_count
        );
    }

    for (const auto& uid : pending_uids) {
        const auto eml_path = options_.save_dir / ("mail_uid_" + uid + ".eml");

        const std::string fetch_command =
            options_.mark_as_seen
                ? "UID FETCH " + uid + " BODY[]"
                : "UID FETCH " + uid + " BODY.PEEK[]";

        std::string fetch_response;
        std::string fetch_error;

        if (!performImapCommand(account_, imap_url, fetch_command, fetch_response, fetch_error)) {
            result.error_message += "fetch uid " + uid + " failed: " + fetch_error + "\n";
            continue;
        }

        std::string eml_content;
        if (!extractFirstImapLiteral(fetch_response, eml_content)) {
            result.error_message += "extract uid " + uid + " literal failed\n";
            continue;
        }

        std::ofstream file(eml_path, std::ios::binary);
        if (!file.is_open()) {
            result.error_message += "open file failed: " + eml_path.string() + "\n";
            continue;
        }

        file.write(eml_content.data(), static_cast<std::streamsize>(eml_content.size()));
        file.close();

        insertMailState(storage, account_.username, account_.mailbox, uid, eml_path);

        result.mails.push_back(Mail{
            uid,
            eml_path
        });
    }

    result.success = result.error_message.empty();
    return result;
}
