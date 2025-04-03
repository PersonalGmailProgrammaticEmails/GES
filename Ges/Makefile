SRC_DIR=src

#Put the makefile lower
PROTO_DIR=../GesProtobuf

proto_files := $(shell find $(PROTO_DIR) -name *.pb.cc -print)
proto_header_files := $(shell find $(PROTO_DIR) -name *.pb.h -print)
src_files := $(shell find $(SRC_DIR) -name *.cpp -print)
#Shouldn't need the header files here
header_files := $(shell find $(SRC_DIR) -name *.h -print)

LIBS += -lcurl \
        -lspdlog \
        -lfmt \
        -lgrpc++ \
        -lprotobuf

.PHONY: all clean


all:
	g++ --std=c++20 -o ges -O3 -I $(SRC_DIR) -I $(PROTO_DIR) $(src_files) $(proto_files) $(LIBS)

debug:
	g++ --std=c++20 -o ges-debug -O0 -g -I $(SRC_DIR) -I $(PROTO_DIR) $(src_files) $(proto_files) $(LIBS)

clean:
	rm -f ges ges-debug
