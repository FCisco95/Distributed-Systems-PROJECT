SHELL=/bin/bash

OBJ_DIR=object
SRC_DIR=source
INC_DIR=include
BIN_DIR=binary
TESTS_DIR=tests
LIB_DIR=lib
PROTOBUF_GEN=protobuf_generated

CC=gcc
CFLAGS = -O0 -g -Wall -I$(INC_DIR) -I$(PROTOBUF_GEN) -D THREADED
# caminhos para onde estão os .h
#CFLAGS += -I$(HOME)/zookeeper/zookeeper-client/zookeeper-client-c/include/ -I$(HOME)/zookeeper/zookeeper-client/zookeeper-client-c/generated/

OBJECTS=$(addprefix $(OBJ_DIR)/, data.o entry.o tree.o tree_skel.o sdmessage.pb-c.o) 
OBJECTS += $(addprefix $(OBJ_DIR)/, test_data.o test_entry.o test_tree.o)
OBJECTS += $(addprefix $(OBJ_DIR)/, network_server.o message.o tree_server.o client_stub.o  network_client.o  tree_client.o)

LIBS=$(addprefix $(LIB_DIR)/, client-lib.o)

BINARIES=$(addprefix $(BIN_DIR)/, test_data test_entry test_tree tree-client tree-server)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TESTS_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(PROTOBUF_GEN)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

all: $(BINARIES) $(LIB_DIR)/client-lib.o

$(BIN_DIR)/test_data: $(addprefix $(OBJ_DIR)/, data.o test_data.o)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/test_entry: $(addprefix $(OBJ_DIR)/, data.o entry.o test_entry.o)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/test_tree: $(addprefix $(OBJ_DIR)/, data.o entry.o tree.o test_tree.o)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/tree-server: $(addprefix $(OBJ_DIR)/, tree_server.o network_server.o data.o entry.o tree.o message.o tree_skel.o network_client.o client_stub.o sdmessage.pb-c.o)
	$(CC) $(CFLAGS) $^ -o $@ -lprotobuf-c -lpthread -lzookeeper_mt

$(BIN_DIR)/tree-client: $(addprefix $(OBJ_DIR)/, tree_client.o client_stub.o network_client.o data.o entry.o tree.o message.o sdmessage.pb-c.o)
	$(CC) $(CFLAGS) $^ -o $@ -lprotobuf-c -lzookeeper_mt

    
$(OBJECTS): | $(OBJ_DIR)
$(BINARIES): | $(BIN_DIR)
$(LIBS): | $(LIB_DIR)

$(OBJ_DIR)/data.o: $(SRC_DIR)/data.c $(addprefix $(INC_DIR)/, data.h)
$(OBJ_DIR)/entry.o: $(SRC_DIR)/entry.c $(addprefix $(INC_DIR)/, entry.h)
$(OBJ_DIR)/tree.o: $(SRC_DIR)/tree.c $(addprefix $(INC_DIR)/, tree-private.h)
$(OBJ_DIR)/tree_skel.o: $(SRC_DIR)/tree_skel.c $(addprefix $(INC_DIR)/, message-private.h) $(PROTOBUF_GEN)/sdmessage.pb-c.h
$(OBJ_DIR)/sdmessage.pb-c.o : $(PROTOBUF_GEN)/sdmessage.pb-c.c $(PROTOBUF_GEN)/sdmessage.pb-c.h
$(OBJ_DIR)/message.o : $(SRC_DIR)/message.c $(addprefix $(INC_DIR)/, message-private.h) $(PROTOBUF_GEN)/sdmessage.pb-c.h

$(OBJ_DIR)/network_server.o: $(SRC_DIR)/network_server.c $(addprefix $(INC_DIR)/, tree_skel.h) $(PROTOBUF_GEN)/sdmessage.pb-c.h

$(OBJ_DIR)/network_client.o: $(SRC_DIR)/network_client.c $(addprefix $(INC_DIR)/, message-private.h network_client.h) $(PROTOBUF_GEN)/sdmessage.pb-c.h
$(OBJ_DIR)/client_stub.o: $(SRC_DIR)/client_stub.c $(addprefix $(INC_DIR)/, client_stub-private.h) $(PROTOBUF_GEN)/sdmessage.pb-c.h

$(OBJ_DIR)/test_data.o: $(TESTS_DIR)/test_data.c
$(OBJ_DIR)/test_entry.o: $(TESTS_DIR)/test_entry.c
$(OBJ_DIR)/test_tree.o: $(TESTS_DIR)/test_tree.c
$(OBJ_DIR)/tree_server.o: $(SRC_DIR)/tree_server.c $(PROTOBUF_GEN)/sdmessage.pb-c.h
$(OBJ_DIR)/tree_client.o: $(SRC_DIR)/tree_client.c $(PROTOBUF_GEN)/sdmessage.pb-c.h

$(LIB_DIR)/client-lib.o: $(addprefix $(OBJ_DIR)/, client_stub.o network_client.o data.o entry.o)
	ld -r $^ -o $(LIB_DIR)/client-lib.o

$(PROTOBUF_GEN)/sdmessage.pb-c.h: sdmessage.proto
	protoc-c --c_out=$(PROTOBUF_GEN) sdmessage.proto

protobuf:
	protoc-c --c_out=$(PROTOBUF_GEN) sdmessage.proto

clean:
	rm -vf $(BINARIES)
	rm -vf $(OBJECTS)
	rm -vf $(PROTOBUF_GEN)/sdmessage*
	rm -vf $(LIBS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(TESTS_DIR):
	mkdir -p $(TESTS_DIR)

