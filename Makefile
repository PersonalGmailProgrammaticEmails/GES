
PROTO_DIR=GesProtobuf
GES_DIR=Ges

all: ges protos

.PHONY: all protos ges clean

protos:
	$(MAKE) -C $(PROTO_DIR)

ges: protos
	$(MAKE) -C $(GES_DIR)

clean:
	$(MAKE) -C $(PROTO_DIR) clean
	$(MAKE) -C $(GES_DIR) clean
