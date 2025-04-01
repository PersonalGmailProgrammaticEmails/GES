PROTO_DIR=protos
SRC_DIR=src

proto_files := $(shell find $(PROTO_DIR) -name *.proto -print)
src_files := $(shell find $(SRC_DIR) -name *.cpp -print)
#Shouldn't need the header files here
header_files := $(shell find $(SRC_DIR) -name *.h -print)

proto_src_files := $(patsubst $(PROTO_DIR)/%.proto,$(SRC_DIR)/%.pb.cc,$(proto_files))
proto_header_files := $(patsubst $(PROTO_DIR)/%.proto,$(SRC_DIR)/%.pb.h,$(proto_files))
grpc_src_files := $(patsubst $(PROTO_DIR)/%.proto,$(SRC_DIR)/%.grpc.pb.cc,$(proto_files))
grpc_header_files := $(patsubst $(PROTO_DIR)/%.proto,$(SRC_DIR)/%.grpc.pb.h,$(proto_files))

LIBS += -lcurl \
        -lspdlog \
        -lfmt \
        -lgrpc++ \
        -lprotobuf

.PHONY: all clean

#%.pb.cc %.pb.h %.grpc.pb.h %.grpc.pb.cc: %.proto
#	protoc -I $(PROTO_DIR) --cpp_out=$(SRC_DIR) --grpc-out=$(SRC_DIR) --plugin=protoc-gen-grpc=grpc_cpp_plugin $<
#
$(SRC_DIR)/%.pb.cc: $(PROTO_DIR)/%.proto
	protoc -I $(PROTO_DIR) --cpp_out=$(SRC_DIR) --grpc_out=$(SRC_DIR) --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin $<

all: $(proto_src_files) $(proto_header_files) $(grpc_src_files) $(grpc_header_files)
	g++ --std=c++20 -o ges -O3 -I $(SRC_DIR) $(src_files) $(proto_src_files) $(grpc_src_files) $(LIBS)

debug:
	g++ --std=c++20 -o ges-debug -O0 -g -I $(SRC_DIR) $(src_files) $(proto_src_files) $(grpc_src_files) $(LIBS)

clean:
	rm -f ges ges-debug
	rm src/*.pb.*
