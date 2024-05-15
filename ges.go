package main

import (
    //"flag"
    "context"
    "encoding/json"
    "fmt"
    "log"
    "net/http"
    "os"

    "golang.org/x/oauth2"
    "golang.org/x/oauth2/google"
    "google.golang.org/api/gmail/v1"
    "google.golang.org/api/option"
)

// Retrieve a token, saves the token, then returns the generated client.
func getClient(config *oauth2.Config, auth_key chan string) *http.Client {
    // The file token.json stores the user's access and refresh tokens, and is
    // created automatically when the authorization flow completes for the first
    // time.
    homedir := os.Getenv("HOME")
    tokFile := homedir + "/.google_token_credentials.json"
    tok, err := tokenFromFile(tokFile)
    if err != nil {
            tok = getTokenFromWeb(config, auth_key)
            saveToken(tokFile, tok)
    }
    return config.Client(context.Background(), tok)
}

// Request a token from the web, then returns the retrieved token.
func getTokenFromWeb(config *oauth2.Config, auth_key chan string) *oauth2.Token {
    authURL := config.AuthCodeURL("state-token", oauth2.AccessTypeOffline)
    fmt.Printf("Go to the following link in your browser then type the "+
            "authorization code: \n%v\n\n\n", authURL)

    authCode := <-auth_key
    //if _, err := fmt.Scan(&authCode); err != nil {
    //        log.Fatalf("Unable to read authorization code: %v", err)
    //}

    tok, err := config.Exchange(context.TODO(), authCode)
    if err != nil {
            log.Fatalf("Unable to retrieve token from web: %v", err)
    }
    return tok
}

// Retrieves a token from a local file.
func tokenFromFile(file string) (*oauth2.Token, error) {
    f, err := os.Open(file)
    if err != nil {
            return nil, err
    }
    defer f.Close()
    tok := &oauth2.Token{}
    err = json.NewDecoder(f).Decode(tok)
    return tok, err
}

// Saves a token to a file path.
func saveToken(path string, token *oauth2.Token) {
    fmt.Printf("Saving credential file to: %s\n", path)
    f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0600)
    if err != nil {
            log.Fatalf("Unable to cache oauth token: %v", err)
    }
    defer f.Close()
    json.NewEncoder(f).Encode(token)
}

func pickUpAuthCodeWrapper(finished_chan chan string) (func (http.ResponseWriter, *http.Request)) {
    return func(w http.ResponseWriter, req *http.Request) {
        pickUpAuthCode(w, req, finished_chan)
    }
}

func pickUpAuthCode(w http.ResponseWriter, req *http.Request, final_resp chan string) {
    //fmt.Printf("The auth code query values: %s\n", req.URL.Query()["code"])
    //fmt.Printf("Just the first: %s\n", req.URL.Query()["code"][0])
    final_resp <- req.URL.Query()["code"][0]
    //os.Stdin.Write([]byte(req.URL.Query()["code"][0]))
    //os.Stdin.Write([]byte("\n"))
}

func main() {
    //authKey := flag.String("auth", "", "The authorization key")

    //flag.Parse()

    ctx := context.Background()
    home_dir := os.Getenv("HOME")
    credential_dir := home_dir + "/.google_credentials.json"
    b, err := os.ReadFile(credential_dir)
    if err != nil {
            log.Fatalf("Unable to read client secret file: %v", err)
    }
    auth_key := make(chan string)
    
    the_server := http.Server {
        Addr: "localhost:80",
    }
    http.DefaultServeMux.HandleFunc("/", pickUpAuthCodeWrapper(auth_key))


    go func() {
        //http.HandleFunc("/", pickUpAuthCodeWrapper(auth_key))
        fmt.Printf("Ready to listen...\n")
        err := the_server.ListenAndServe()
        if err != http.ErrServerClosed {
            log.Fatal(err)
        }
        fmt.Printf("Server closed\n")
        //log.Fatal(http.ListenAndServe("localhost:80", nil))
    }()

    // If modifying these scopes, delete your previously saved token.json.
    config, err := google.ConfigFromJSON(b, gmail.GmailSendScope, gmail.GmailReadonlyScope)
    if err != nil {
            log.Fatalf("Unable to parse client secret file to config: %v", err)
    }
    client := getClient(config, auth_key)

    fmt.Printf("Shutting the server down...")
    the_server.Shutdown(ctx)

    srv, err := gmail.NewService(ctx, option.WithHTTPClient(client))
    if err != nil {
            log.Fatalf("Unable to retrieve Gmail client: %v", err)
    }

    user := "me"
    r, err := srv.Users.Labels.List(user).Do()
    if err != nil {
            log.Fatalf("Unable to retrieve labels: %v", err)
    }
    if len(r.Labels) == 0 {
            fmt.Println("No labels found.")
            return
    }
    fmt.Println("Labels:")
    for _, l := range r.Labels {
            fmt.Printf("- %s\n", l.Name)
    }
}
