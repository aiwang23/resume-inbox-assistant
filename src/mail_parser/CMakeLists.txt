add_library(mail_parser STATIC
        mail_parser.cpp
)

target_include_directories(mail_parser PUBLIC
        .
)

target_link_libraries(mail_fetcher PRIVATE
        
)
