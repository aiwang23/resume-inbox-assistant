#ifndef MAIL_PARSER_H
#define MAIL_PARSER_H

#include <filesystem>
#include <string>
#include <vector>

class MailParser {
public:
    struct Options {
        std::filesystem::path attachment_save_dir = "./attachments";
        bool save_attachments = true;
    };

    struct Attachment {
        std::string filename;
        std::string content_type;
        std::filesystem::path saved_path;
        std::size_t size = 0;
    };

    struct MailContent {
        std::string subject;
        std::string from;
        std::string to;

        std::string text_body;
        std::string html_body;

        std::vector<Attachment> attachments;
    };

    struct Result {
        bool success = false;
        std::string error_message;
        MailContent content;
    };

public:
    MailParser();
    ~MailParser();

    Result parseFile(const std::filesystem::path& eml_path);

    Result parseFile(const std::filesystem::path& eml_path, const Options& options);
};

#endif // MAIL_PARSER_H