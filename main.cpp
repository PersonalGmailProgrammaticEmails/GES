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

// Define a static payload struct to avoid scope issues
struct UploadStatus {
    const char* data;
    size_t size;
};

// Callback function for reading the payload
static size_t ReadCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct UploadStatus* upload_ctx = (struct UploadStatus*)userdata;
    size_t max_size = size * nmemb;
    
    if (upload_ctx->size == 0) {
        return 0; // No more data to send
    }
    
    size_t copy_size = upload_ctx->size;
    if (copy_size > max_size) {
        copy_size = max_size;
    }
    
    memcpy(ptr, upload_ctx->data, copy_size);
    upload_ctx->data += copy_size;
    upload_ctx->size -= copy_size;
    
    return copy_size;
}

// Callback function for libcurl to handle the server response
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
        // Log the response in real-time for debugging
        spdlog::info("SERVER: {}", std::string((char*)contents, newLength));
    } catch (const std::exception& e) {
        spdlog::error("Exception in WriteCallback: {}", e.what());
        return 0;
    }
    return newLength;
}

int main(int argc, char* argv[]) {
    // Initialize logger
    try {
        auto console = spdlog::stdout_color_mt("console");
        spdlog::set_default_logger(console);
        spdlog::set_level(spdlog::level::debug);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    }

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

    // Initialize curl
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res != CURLE_OK) {
        spdlog::error("curl_global_init() failed: {}", curl_easy_strerror(res));
        return 1;
    }

    // Set up the curl session
    CURL *curl = curl_easy_init();
    if (!curl) {
        spdlog::error("curl_easy_init() failed");
        curl_global_cleanup();
        return 1;
    }
    
    spdlog::debug("curl initialized successfully");

    // Create the email payload
    // Adding To: header with all recipients
    std::string to_header = "To: ";
    for (size_t i = 0; i < recipients.size(); ++i) {
        to_header += recipients[i];
        if (i < recipients.size() - 1) {
            to_header += ", ";
        }
    }
    
    std::string payload_str = "From: " + username + "\r\n" +
                        to_header + "\r\n" +
                        "Subject: " + subject + "\r\n" +
                        "Content-Type: text/plain; charset=UTF-8\r\n" +
                        "\r\n" +
                        message;
    
    UploadStatus upload_ctx = {payload_str.c_str(), payload_str.size()};
    
    // Create a libcurl recipients list
    struct curl_slist *recipients_list = NULL;
    for (const auto& recipient : recipients) {
        recipients_list = curl_slist_append(recipients_list, recipient.c_str());
        if (!recipients_list) {
            spdlog::error("Failed to add recipient: {}", recipient);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 1;
        }
    }
    
    // Set up the curl options
    try {
        // Enable verbose output for more detailed debugging
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Set the SMTP server (Gmail)
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        
        // Set the username and password
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
        
        // Set SSL verification (required for Gmail)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Set the sender
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username.c_str());
        
        // Set the recipients
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients_list);
        
        // Set the payload read callback
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        
        // Set the response callback
        std::string readBuffer;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        
        // Disable timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        
        // Set error buffer
        char error_buffer[CURL_ERROR_SIZE];
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
        
        spdlog::info("==== SENDING EMAIL ====");
        spdlog::info("Subject: {}", subject);
        spdlog::info("Sending to {} recipients", recipients.size());
        
        // Send the email
        res = curl_easy_perform(curl);
        
        // Check for errors
        if (res != CURLE_OK) {
            spdlog::error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
            spdlog::error("Error buffer: {}", error_buffer);
        } else {
            spdlog::info("Email sent successfully!");
        }
        
        // Display full server response buffer
        spdlog::info("==== COMPLETE SERVER RESPONSE ====\n{}", readBuffer);
    } catch (const std::exception& e) {
        spdlog::error("Exception during curl operations: {}", e.what());
    }
    
    // Clean up
    if (recipients_list) {
        curl_slist_free_all(recipients_list);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    return 0;
}
