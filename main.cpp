#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp> // For JSON parsing, you might need to install this library

// Using json library for easier parsing
using json = nlohmann::json;

/**
 * OAuth2 configuration
 */
struct OAuth2Config {
    std::string client_id;
    std::string client_secret;
    std::string redirect_uri;
    std::string auth_uri = "https://accounts.google.com/o/oauth2/auth";
    std::string token_uri = "https://oauth2.googleapis.com/token";
    std::string scope = "https://mail.google.com/";
    std::string refresh_token;
    std::string access_token;
    time_t token_expiry = 0;
};

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

// Read OAuth2 configuration from a JSON file
bool readOAuth2Config(const std::string& filename, OAuth2Config& config) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            spdlog::error("Failed to open OAuth2 config file: {}", filename);
            return false;
        }

        json j;
        file >> j;

        config.client_id = j["client_id"];
        config.client_secret = j["client_secret"];
        config.redirect_uri = j["redirect_uri"];
        
        if (j.contains("refresh_token") && !j["refresh_token"].empty()) {
            config.refresh_token = j["refresh_token"];
        }
        
        if (j.contains("access_token") && !j["access_token"].empty()) {
            config.access_token = j["access_token"];
        }
        
        if (j.contains("token_expiry") && j["token_expiry"] > 0) {
            config.token_expiry = j["token_expiry"];
        }
        
        if (j.contains("auth_uri") && !j["auth_uri"].empty()) {
            config.auth_uri = j["auth_uri"];
        }
        
        if (j.contains("token_uri") && !j["token_uri"].empty()) {
            config.token_uri = j["token_uri"];
        }
        
        if (j.contains("scope") && !j["scope"].empty()) {
            config.scope = j["scope"];
        }
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception reading OAuth2 config: {}", e.what());
        return false;
    }
}

// Save OAuth2 configuration to a JSON file
bool saveOAuth2Config(const std::string& filename, const OAuth2Config& config) {
    try {
        json j;
        j["client_id"] = config.client_id;
        j["client_secret"] = config.client_secret;
        j["redirect_uri"] = config.redirect_uri;
        j["refresh_token"] = config.refresh_token;
        j["access_token"] = config.access_token;
        j["token_expiry"] = config.token_expiry;
        j["auth_uri"] = config.auth_uri;
        j["token_uri"] = config.token_uri;
        j["scope"] = config.scope;
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            spdlog::error("Failed to open OAuth2 config file for writing: {}", filename);
            return false;
        }
        
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception saving OAuth2 config: {}", e.what());
        return false;
    }
}

// Callback function for libcurl to handle the server response
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch (const std::exception& e) {
        spdlog::error("Exception in WriteCallback: {}", e.what());
        return 0;
    }
    return newLength;
}

// Callback function for reading from a buffer
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

// Generate authorization URL
std::string generateAuthorizationUrl(const OAuth2Config& config) {
    std::string url = config.auth_uri + 
                      "?client_id=" + config.client_id +
                      "&redirect_uri=" + config.redirect_uri +
                      "&response_type=code" +
                      "&scope=" + config.scope +
                      "&access_type=offline" +
                      "&prompt=consent";
    return url;
}

// Exchange authorization code for tokens
bool exchangeCodeForTokens(OAuth2Config& config, const std::string& auth_code) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        spdlog::error("Failed to initialize curl");
        return false;
    }
    
    std::string postFields = "code=" + auth_code +
                             "&client_id=" + config.client_id +
                             "&client_secret=" + config.client_secret +
                             "&redirect_uri=" + config.redirect_uri +
                             "&grant_type=authorization_code";
    
    std::string response_data;
    
    curl_easy_setopt(curl, CURLOPT_URL, config.token_uri.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        spdlog::error("Failed to exchange code for tokens: {}", curl_easy_strerror(res));
        return false;
    }
    
    try {
        spdlog::debug("Token response: {}", response_data);
        
        json response = json::parse(response_data);
        
        if (response.contains("access_token")) {
            config.access_token = response["access_token"];
            
            if (response.contains("refresh_token")) {
                config.refresh_token = response["refresh_token"];
            }
            
            if (response.contains("expires_in")) {
                int expires_in = response["expires_in"];
                config.token_expiry = time(nullptr) + expires_in;
            }
            
            return true;
        } else if (response.contains("error")) {
            std::string error = response["error"];
            std::string error_description = "";
            if (response.contains("error_description")) {
                error_description = response["error_description"];
            }
            spdlog::error("OAuth2 error: {} - {}", error, error_description);
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception parsing token response: {}", e.what());
    }
    
    return false;
}

// Refresh the access token using a refresh token
bool refreshAccessToken(OAuth2Config& config) {
    if (config.refresh_token.empty()) {
        spdlog::error("No refresh token available");
        return false;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        spdlog::error("Failed to initialize curl");
        return false;
    }
    
    std::string postFields = "client_id=" + config.client_id +
                             "&client_secret=" + config.client_secret +
                             "&refresh_token=" + config.refresh_token +
                             "&grant_type=refresh_token";
    
    std::string response_data;
    
    curl_easy_setopt(curl, CURLOPT_URL, config.token_uri.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        spdlog::error("Failed to refresh access token: {}", curl_easy_strerror(res));
        return false;
    }
    
    try {
        spdlog::debug("Refresh token response: {}", response_data);
        
        json response = json::parse(response_data);
        
        if (response.contains("access_token")) {
            config.access_token = response["access_token"];
            
            if (response.contains("expires_in")) {
                int expires_in = response["expires_in"];
                config.token_expiry = time(nullptr) + expires_in;
            }
            
            return true;
        } else if (response.contains("error")) {
            std::string error = response["error"];
            std::string error_description = "";
            if (response.contains("error_description")) {
                error_description = response["error_description"];
            }
            spdlog::error("OAuth2 error: {} - {}", error, error_description);
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception parsing refresh token response: {}", e.what());
    }
    
    return false;
}

// Ensure a valid access token is available, refreshing if necessary
bool ensureValidAccessToken(OAuth2Config& config) {
    time_t now = time(nullptr);
    
    // If token is still valid with at least 60 seconds margin
    if (!config.access_token.empty() && config.token_expiry > now + 60) {
        spdlog::info("Access token is still valid");
        return true;
    }
    
    // If refresh token is available, try to refresh the access token
    if (!config.refresh_token.empty()) {
        spdlog::info("Refreshing access token...");
        return refreshAccessToken(config);
    }
    
    spdlog::error("No valid access token and no refresh token");
    return false;
}

// Define a static payload struct to avoid scope issues
struct UploadStatus {
    const char* data;
    size_t size;
};

// Function to send an email using OAuth2 authentication
bool sendEmail(const OAuth2Config& config, 
               const std::string& username, 
               const std::vector<std::string>& recipients,
               const std::string& subject, 
               const std::string& message) {
    
    // Initialize curl
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res != CURLE_OK) {
        spdlog::error("curl_global_init() failed: {}", curl_easy_strerror(res));
        return false;
    }

    // Set up the curl session
    CURL *curl = curl_easy_init();
    if (!curl) {
        spdlog::error("curl_easy_init() failed");
        curl_global_cleanup();
        return false;
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
            return false;
        }
    }
    
    bool success = false;
    
    // Set up the curl options
    try {
        // Enable verbose output for more detailed debugging
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Set the SMTP server (Gmail)
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        
        // Set the username 
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        
        // Set the OAuth2 bearer token
        curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, config.access_token.c_str());
        
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
        
        // Set login options to use OAuth2
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, "AUTH=XOAUTH2");
        
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
        spdlog::info("Using OAuth2 authentication");
        
        // Send the email
        res = curl_easy_perform(curl);
        
        // Check for errors
        if (res != CURLE_OK) {
            spdlog::error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
            spdlog::error("Error buffer: {}", error_buffer);
        } else {
            spdlog::info("Email sent successfully!");
            success = true;
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
    
    return success;
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
        spdlog::warn("Usage: {} <gmail_username> <oauth2_config.json> [recipient_file/email] [subject] [message]", argv[0]);
        spdlog::warn("  - oauth2_config.json should contain client_id and client_secret");
        spdlog::warn("  - If recipient_file is provided, emails will be sent to all addresses in the file");
        spdlog::warn("  - If recipient_email is provided directly, it will be used as the recipient");
        return 1;
    }

    std::string username = argv[1];
    std::string config_file = argv[2];
    
    // Load OAuth2 configuration
    OAuth2Config oauth2_config;
    if (!readOAuth2Config(config_file, oauth2_config)) {
        spdlog::error("Failed to read OAuth2 configuration from {}", config_file);
        return 1;
    }
    
    // Initialize recipients list
    std::vector<std::string> recipients;
    std::string subject = "Test Email";
    std::string message = "This is a test email sent from C++ using libcurl with OAuth2 authentication";
    
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
    
    // Check if we have a valid OAuth2 configuration
    if (oauth2_config.client_id.empty() || oauth2_config.client_secret.empty()) {
        spdlog::error("OAuth2 configuration is incomplete. client_id and client_secret are required.");
        return 1;
    }
    
    // Set default redirect URI if not provided
    if (oauth2_config.redirect_uri.empty()) {
        oauth2_config.redirect_uri = "urn:ietf:wg:oauth:2.0:oob";
    }
    
    // Check if we have a valid access token or need to get one
    if (!ensureValidAccessToken(oauth2_config)) {
        // If we can't refresh, we need to get a new authorization code
        std::string auth_url = generateAuthorizationUrl(oauth2_config);
        spdlog::info("Please open the following URL in a browser and authorize the application:");
        spdlog::info("{}", auth_url);
        
        spdlog::info("Enter the authorization code: ");
        std::string auth_code;
        std::getline(std::cin, auth_code);
        
        if (!exchangeCodeForTokens(oauth2_config, auth_code)) {
            spdlog::error("Failed to obtain access token");
            return 1;
        }
        
        // Save the updated configuration with tokens
        if (!saveOAuth2Config(config_file, oauth2_config)) {
            spdlog::error("Failed to save OAuth2 configuration with tokens");
        }
    }
    
    // Now we have a valid access token, send the email
    if (!sendEmail(oauth2_config, username, recipients, subject, message)) {
        spdlog::error("Failed to send email");
        return 1;
    }
    
    return 0;
}
