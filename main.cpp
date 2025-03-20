#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

/**
 * Read recipients from a file, one email address per line
 * @param filename Path to the file containing email addresses
 * @return Vector of email addresses
 */
std::vector<std::string> readRecipientsFromFile(const std::string& filename) {
    std::vector<std::string> recipients;
    std::ifstream file(filename);
    std::string line;

    if (!file.is_open()) {
        spdlog::error("Failed to open recipient file: {}", filename);
        return recipients;
    }

    while (std::getline(file, line)) {
        // Skip empty lines and lines starting with #
        if (!line.empty() && line[0] != '#') {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            if (!line.empty()) {
                spdlog::debug("Adding recipient: {}", line);
                recipients.push_back(line);
            }
        }
    }

    spdlog::info("Read {} recipients from file", recipients.size());
    return recipients;
}

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
    if (argc < 3) {
        spdlog::warn("Usage: {} <gmail_username> <app_password> [recipient_file] [recipient_email] [subject] [message]", argv[0]);
        spdlog::warn("  - If recipient_file is provided, emails will be sent to all addresses in the file");
        spdlog::warn("  - If recipient_email is provided directly, it will be used as the recipient");
        spdlog::info("Note: It's recommended to use Gmail App Passwords instead of your main account password.");
        return 1;
    }

    std::string username = argv[1];
    std::string password = argv[2];
    
    // Initialize recipients list
    std::vector<std::string> recipients;
    std::string subject = "Test Email";
    std::string message = "This is a test email sent from C++ using libcurl";
    
    // Parse command line arguments
    if (argc >= 4) {
        std::string arg3 = argv[3];
        
        // Check if the third argument is a file
        std::ifstream test_file(arg3);
        if (test_file.good()) {
            // It's a file, read recipients from it
            spdlog::info("Reading recipients from file: {}", arg3);
            recipients = readRecipientsFromFile(arg3);
            
            // Set subject and message if provided
            if (argc >= 5) subject = argv[4];
            if (argc >= 6) message = argv[5];
        } else {
            // It's not a file, use it as a direct recipient
            recipients.push_back(arg3);
            
            // Set subject and message if provided
            if (argc >= 5) subject = argv[4];
            if (argc >= 6) message = argv[5];
        }
    }
    
    // Check if we have any recipients
    if (recipients.empty()) {
        spdlog::error("No recipients specified!");
        return 1;
    }

    CURL *curl;
    CURLcode res;
    std::string readBuffer;

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

        // Set the sender
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username.c_str());
        
        // Create recipients list for libcurl
        struct curl_slist *recipients_list = NULL;
        for (const auto& recipient : recipients) {
            recipients_list = curl_slist_append(recipients_list, recipient.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients_list);
        spdlog::debug("Set sender and {} recipients", recipients.size());

        // Create the email payload
        // Adding To: header with all recipients
        std::string to_header = "To: ";
        for (size_t i = 0; i < recipients.size(); ++i) {
            to_header += recipients[i];
            if (i < recipients.size() - 1) {
                to_header += ", ";
            }
        }
        
        std::string payload = "From: " + username + "\r\n" +
                             to_header + "\r\n" +
                             "Subject: " + subject + "\r\n\r\n" +
                             message;
        
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
        spdlog::info("Subject: {}", subject);
        spdlog::info("Sending to {} recipients", recipients.size());
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
        curl_slist_free_all(recipients_list);
        curl_easy_cleanup(curl);
    } else {
        spdlog::error("Failed to initialize curl");
    }
    
    curl_global_cleanup();
    return 0;
}
