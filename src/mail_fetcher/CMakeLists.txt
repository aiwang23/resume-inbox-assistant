add_library(mail_fetcher STATIC
        mail_fetcher.cpp
)

target_include_directories(mail_fetcher PUBLIC
        .
)

target_link_libraries(mail_fetcher PRIVATE
        CURL::libcurl
        sqlite_orm::sqlite_orm
)