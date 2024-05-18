package auth

import (
    //"flag"
    "net/http"
)


func pickUpAuthCode(w http.ResponseWriter, req *http.Request, final_resp chan string) {
    final_resp <- req.URL.Query()["code"][0]
}

func PickUpAuthCodeWrapper(finished_chan chan string) (func (http.ResponseWriter, *http.Request)) {
    return func(w http.ResponseWriter, req *http.Request) {
        pickUpAuthCode(w, req, finished_chan)
    }
}
