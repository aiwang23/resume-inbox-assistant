#include "mail_fetcher.h"
#include "mail_parser.h"

#include <cxxopts.hpp>

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    cxxopts::Options cli("resume-inbox-assistant", "Fetch and parse resume emails");

    cli.add_options()("host", "IMAP server host", cxxopts::value<std::string>())(
        "port", "IMAP server port", cxxopts::value<int>()->default_value("993"))(
        "user", "Email account", cxxopts::value<std::string>())(
        "password", "Email password or app password", cxxopts::value<std::string>())(
        "mailbox", "IMAP mailbox", cxxopts::value<std::string>()->default_value("INBOX"))(
        "ssl", "Use SSL", cxxopts::value<bool>()->default_value("true"))(
        "save-dir", "Directory to save eml files",
        cxxopts::value<std::string>()->default_value("./mails"))(
        "state-db", "SQLite state database path",
        cxxopts::value<std::string>()->default_value("./mail_state.sqlite3"))(
        "max-count", "Max emails to fetch", cxxopts::value<int>()->default_value("5"))(
        "mark-seen", "Mark emails as seen",
        cxxopts::value<bool>()->default_value("false"))("h,help", "Show help");

    auto args = cli.parse(argc, argv);

    if (args.count("help")) {
        std::cout << cli.help() << std::endl;
        return 0;
    }

    if (!args.count("host") || !args.count("user") || !args.count("password")) {
        std::cerr << cli.help() << std::endl;
        return 1;
    }

    MailFetcher fetcher;

    fetcher.setAccount({args["host"].as<std::string>(), args["port"].as<int>(),
                        args["user"].as<std::string>(), args["password"].as<std::string>(),
                        args["mailbox"].as<std::string>(), args["ssl"].as<bool>()});

    MailFetcher::Options options;
    options.save_dir = args["save-dir"].as<std::string>();
    options.state_db = args["state-db"].as<std::string>();
    options.max_count = args["max-count"].as<int>();
    options.fetch_policy = MailFetcher::FetchPolicy::NewByUid;
    options.mark_as_seen = args["mark-seen"].as<bool>();

    fetcher.setOptions(options);

    auto fetch_result = fetcher.fetchNewMails();

    if (!fetch_result.success) {
        std::cerr << fetch_result.error_message << std::endl;
        return 1;
    }

    MailParser parser;

    for (const auto& mail : fetch_result.mails) {
        auto parse_result = parser.parseFile(mail.eml_path);

        if (!parse_result.success) {
            std::cerr << parse_result.error_message << std::endl;
            continue;
        }

        std::cout << "==== " << mail.eml_path << " ====" << std::endl;
        std::cout << "Subject: " << parse_result.content.subject << std::endl;
        std::cout << "From: " << parse_result.content.from << std::endl;
        std::cout << "To: " << parse_result.content.to << std::endl;
        std::cout << "Text: " << parse_result.content.text_body << std::endl;

        for (const auto& attachment : parse_result.content.attachments) {
            std::cout << "Attachment: " << attachment.filename << " -> " << attachment.saved_path
                      << " size=" << attachment.size << std::endl;
        }
    }

    return 0;
}