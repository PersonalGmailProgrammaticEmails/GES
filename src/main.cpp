#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp> // For JSON parsing
#include <ctime>
#include <random>
#include <thread>
#include <chrono>
#include <atomic>
#include <grpcpp/grpcpp.h>

// Include the generated protocol buffer headers
#include "ges_interface.grpc.pb.h"
#include "ges_interface.pb.h"

// Using json library for easier parsing
using json = nlohmann::json;
//using namespace ges;

// Define a static payload struct to avoid scope issues
struct UploadStatus {
    const char* data;
    size_t size;
};

/**
 * Gmail App Password configuration
 */
struct AppPasswordConfig {
    std::string username;
    std::string app_password;
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

// Read App Password configuration from a JSON file
bool readAppPasswordConfig(const std::string& filename, AppPasswordConfig& config) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            spdlog::error("Failed to open App Password config file: {}", filename);
            return false;
        }

        json j;
        file >> j;

        // Check if both required fields exist
        if (!j.contains("username") || !j.contains("app_password")) {
            spdlog::error("Config file must contain both 'username' and 'app_password' fields");
            return false;
        }

        config.username = j["username"];
        config.app_password = j["app_password"];
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception reading App Password config: {}", e.what());
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

// Function to send an email using App Password authentication
bool sendEmail(const AppPasswordConfig& config, 
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
    
    std::string payload_str = "From: " + config.username + "\r\n" +
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
        // Enable verbose output for debugging
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Set the SMTP server (Gmail)
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        
        // Set the username and password for App Password authentication
        curl_easy_setopt(curl, CURLOPT_USERNAME, config.username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, config.app_password.c_str());
        
        // Set SSL verification (required for Gmail)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Set the sender
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, config.username.c_str());
        
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
        spdlog::info("From: {}", config.username);
        spdlog::info("Subject: {}", subject);
        spdlog::info("Sending to {} recipients", recipients.size());
        spdlog::info("Using App Password authentication");
        
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

// Implementation of the EmailService defined in the proto file
class EmailServiceImpl final : public ges_interface::EmailService::Service {
public:
    EmailServiceImpl(const AppPasswordConfig& config) : config_(config), shutdown_requested_(false) {}

    grpc::Status SendEmail(grpc::ServerContext* context, const ges_interface::EmailRequest* request, 
                           ges_interface::EmailResponse* response) override {
        std::vector<std::string> recipients;
        
        // Otherwise, use the recipients directly from the request
        for (int i = 0; i < request->recipients_size(); i++) {
            recipients.push_back(request->recipients(i));
        }
        
        // Validate we have recipients
        if (recipients.empty()) {
            response->set_success(false);
            response->set_error_message("No recipients specified");
            response->set_recipient_count(0);
            return grpc::Status::OK;
        }
        
        // Send the email
        bool success = sendEmail(config_, recipients, request->subject(), request->the_message());
        
        // Populate the response
        response->set_success(success);
        if (!success) {
            response->set_error_message("Failed to send email");
        }
        response->set_recipient_count(recipients.size());
        
        return grpc::Status::OK;
    }

    grpc::Status Shutdown(grpc::ServerContext* context, const ges_interface::ShutdownRequest* request,
                          ges_interface::ShutdownResponse* response) override {
        spdlog::info("Shutdown requested. Reason: {}", request->reason());
        shutdown_requested_ = true;
        response->set_accepted(true);
        return grpc::Status::OK;
    }

    bool isShutdownRequested() const {
        return shutdown_requested_;
    }

private:
    AppPasswordConfig config_;
    std::atomic<bool> shutdown_requested_;
};

int main(int argc, char* argv[]) {
    // Initialize logger
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Check if we have the required arguments
    if (argc < 2) {
        spdlog::error("Usage: {} <app_password_config.json> [server_address]", argv[0]);
        spdlog::error("  - app_password_config.json must contain both 'username' and 'app_password' fields");
        spdlog::error("  - server_address is optional (default: 0.0.0.0:50051)");
        return 1;
    }

    std::string config_file = argv[1];
    std::string server_address = "0.0.0.0:50051";
    
    // Allow custom server address if provided
    if (argc >= 3) {
        server_address = argv[2];
    }
    
    // Load App Password configuration
    AppPasswordConfig app_password_config;
    if (!readAppPasswordConfig(config_file, app_password_config)) {
        spdlog::error("Failed to read App Password configuration from {}", config_file);
        spdlog::error("Make sure the config file contains both 'username' and 'app_password' fields");
        return 1;
    }
    
    // Create the service implementation
    EmailServiceImpl service(app_password_config);
    
    // Set up the server
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    // Start the server
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    spdlog::info("Email Service server started on {}", server_address);
    spdlog::info("Using email account: {}", app_password_config.username);
    
    // Wait for the server to be shutdown
    while (!service.isShutdownRequested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Shutdown the server
    spdlog::info("Shutting down server...");
    server->Shutdown();
    
    return 0;
}
