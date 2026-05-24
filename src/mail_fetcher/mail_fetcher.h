#ifndef MAIL_FETCHER_H
#define MAIL_FETCHER_H

#include <filesystem>
#include <string>
#include <vector>

class MailFetcher {
public:
    enum class FetchPolicy {
        NewByUid,  // 默认：拉取本软件没处理过的邮件
        UnseenOnly // 只拉邮箱服务器上未读的邮件
    };

    struct Account {
        std::string host;     // 例如: imap.qq.com
        int port = 993;       // IMAPS 默认 993
        std::string username; // 邮箱账号
        std::string password; // 邮箱授权码
        std::string mailbox = "INBOX";
        bool use_ssl = true;
    };

    struct Options {
        std::filesystem::path save_dir = "./mails";              // 保存 .eml
        std::filesystem::path state_db = "./mail_state.sqlite3"; // 记录已处理 UID
        int max_count = 20;
        FetchPolicy fetch_policy = FetchPolicy::NewByUid;
        bool mark_as_seen = false; // 默认不影响邮箱已读状态
    };

    struct Mail {
        std::string uid;
        std::filesystem::path eml_path;
    };

    struct Result {
        bool success = false;
        std::string error_message;
        std::vector<Mail> mails;
    };

public:
    MailFetcher();
    ~MailFetcher();

    void setAccount(const Account& account);
    void setOptions(const Options& options);

    void nb();

    Result fetchNewMails();

private:
    std::string buildImapUrl() const;

private:
    Account account_;
    Options options_;
};

#endif // MAIL_FETCHER_H