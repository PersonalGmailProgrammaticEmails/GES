#include <iostream>
#include <string>
#include <curl/curl.h>

// Callback function for libcurl to handle the server response
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    // Print the response in real-time for debugging
    std::cout << "SERVER: " << std::string((char*)contents, newLength);
    return newLength;
}

int main(int argc, char* argv[]) {
    // Check if we have the required arguments
    if (argc < 6) {
        std::cout << "Usage: " << argv[0] << " <gmail_username> <app_password> <recipient> <subject> <message>" << std::endl;
        std::cout << "Note: It's recommended to use Gmail App Passwords instead of your main account password." << std::endl;
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
        std::cout << "DEBUG: curl initialized successfully" << std::endl;
        
        // Enable verbose output for more detailed debugging
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        
        // Set the SMTP server (Gmail)
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        std::cout << "DEBUG: Set SMTP server to smtp.gmail.com:465" << std::endl;

        // Set the username and password
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
        std::cout << "DEBUG: Set authentication credentials" << std::endl;

        // Set SSL verification (required for Gmail)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        std::cout << "DEBUG: Configured SSL settings" << std::endl;

        // Set the sender and recipient
        struct curl_slist *recipients = NULL;
        recipients = curl_slist_append(recipients, recipient.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
        std::cout << "DEBUG: Set sender and recipient" << std::endl;

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
        std::cout << "DEBUG: Configured email payload" << std::endl;

        // Set up the callback function to handle the server response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        std::cout << "DEBUG: Set up response callback" << std::endl;

        // Send the email
        std::cout << "\n==== SENDING EMAIL ====\n" << std::endl;
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "\nERROR: curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            std::cout << "\nSUCCESS: Email sent successfully!" << std::endl;
        }

        // Display full server response buffer
        std::cout << "\n==== COMPLETE SERVER RESPONSE ====\n" << readBuffer << std::endl;

        // Clean up
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "ERROR: Failed to initialize curl" << std::endl;
    }
    
    curl_global_cleanup();
    return 0;
}
