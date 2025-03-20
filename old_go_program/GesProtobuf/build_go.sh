#!/bin/bash

./clean_go.sh

protoc --go_out=.  --go-grpc_out=. ./ges.proto
