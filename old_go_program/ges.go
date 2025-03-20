package main

import (
    "flag"
    "context"
    "fmt"
    "log"
    "net/http"
    "os"
    "net"

    "golang.org/x/oauth2/google"
    "google.golang.org/api/gmail/v1"
    "google.golang.org/api/option"

    "PersonalGmailProgrammaticEmails/std/ges/internal/auth"

    //"google.golang.org/grpc"
    "google.golang.org/grpc"
	pb "PersonalGmailProgrammaticEmails/std/ges/GesProtobuf/ges_protos"
)

var (
	port = flag.Int("port", 50052, "The GES grpc server port")
)

type server struct {
    pb.UnimplementedGesProtoServiceServer
}


func (s *server) SendEmail(ctx context.Context, in *pb.SendEmailRequest) (*pb.SendEmailResponse, error) {
    log.Printf("Received: %v", in)
    return &pb.SendEmailResponse{}, nil
}


func main() {

    flag.Parse()

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
    http.DefaultServeMux.HandleFunc("/", auth.PickUpAuthCodeWrapper(auth_key))


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

    fmt.Printf("Shutting the server down...\n")
    the_server.Shutdown(ctx)

    srv, err := gmail.NewService(ctx, option.WithHTTPClient(client))
    if err != nil {
            log.Fatalf("Unable to retrieve Gmail client: %v", err)
    }

    //TODO: Remove this
    _ = srv

    /*user := "me"
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
    }*/

    //Set up grpc server
	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
	if err != nil {
		log.Fatalf("failed to listen: %v", err)
	}

	s := grpc.NewServer()
    go_server := &server{}

	pb.RegisterGesProtoServiceServer(s, go_server)
	log.Printf("Grpc server listening at %v", lis.Addr())
	if err := s.Serve(lis); err != nil {
		log.Fatalf("failed to serve: %v", err)
	}

}
