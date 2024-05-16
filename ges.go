package main

import (
    //"flag"
    "context"
    "fmt"
    "log"
    "net/http"
    "os"

    "golang.org/x/oauth2/google"
    "google.golang.org/api/gmail/v1"
    "google.golang.org/api/option"

    "erichCompSci/std/ges/internal/auth"
)

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
    client := auth.GetClient(config, auth_key)

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
