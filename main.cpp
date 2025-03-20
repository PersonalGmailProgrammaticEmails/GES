#include <iostream>
#include <string>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Callback function for libcurl to handle the server response
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    // Log the response in real-time for debugging
    spdlog::info("SERVER: {}", std::string((char*)contents, newLength));
    return newLength;
}

int main(int argc, char* argv[]) {
    // Initialize logger
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Check if we have the required arguments
    if (argc < 6) {
        spdlog::warn("Usage: {} <gmail_username> <app_password> <recipient> <subject> <message>", argv[0]);
        spdlog::info("Note: It's recommended to use Gmail App Passwords instead of your main account password.");
        return 1;
    }

    std::string username = argv[1];
    std::string password = argv[2];
    std::string recipient = argv[3];
    std::string subject = argv[4];
    std::string message = argv[5];

    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    // Create the email payload
    std::string payload = "From: " + username + "\r\n" +
                         "To: " + recipient + "\r\n" +
                         "Subject: " + subject + "\r\n\r\n" +
                         message;

    // Initialize curl
    curl_global_init(CURL_GLOBAL_ALL);
    
    // Set up the curl session
    curl = curl_easy_init();
    if (curl) {
        spdlog::debug("curl initialized successfully");
        
        // Enable verbose output for more detailed debugging
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Set the SMTP server (Gmail)
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        spdlog::debug("Set SMTP server to smtp.gmail.com:465");

        // Set the username and password
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
        spdlog::debug("Set authentication credentials");

        // Set SSL verification (required for Gmail)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        spdlog::debug("Configured SSL settings");

        // Set the sender and recipient
        struct curl_slist *recipients = NULL;
        recipients = curl_slist_append(recipients, recipient.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
        spdlog::debug("Set sender and recipient");

        // Set the payload
        curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, 
            [](void *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
                std::string *payload = static_cast<std::string*>(userdata);
                if (payload->size() > 0) {
                    size_t len = payload->size() < size * nmemb ? payload->size() : size * nmemb;
                    memcpy(ptr, payload->c_str(), len);
                    payload->erase(0, len);
                    return len;
                }
                return 0;
            });
        spdlog::debug("Configured email payload");

        // Set up the callback function to handle the server response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        spdlog::debug("Set up response callback");

        // Send the email
        spdlog::info("==== SENDING EMAIL ====");
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK) {
            spdlog::error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        } else {
            spdlog::info("Email sent successfully!");
        }

        // Display full server response buffer
        spdlog::info("==== COMPLETE SERVER RESPONSE ====\n{}", readBuffer);

        // Clean up
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    } else {
        spdlog::error("Failed to initialize curl");
    }
    
    curl_global_cleanup();
    return 0;
}
