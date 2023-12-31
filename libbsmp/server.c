#include "server.h"
#include "server_priv.h"
#include "bsmp_priv.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void server_init (struct bsmp_server *server)
{
    memset(server, 0, sizeof(*server));

    group_init(&server->groups.list[GROUP_ALL_ID],   GROUP_ALL_ID);
    group_init(&server->groups.list[GROUP_READ_ID],  GROUP_READ_ID);
    group_init(&server->groups.list[GROUP_WRITE_ID], GROUP_WRITE_ID);

    server->groups.count = GROUP_STANDARD_COUNT;
}

bsmp_server_t *bsmp_server_new (void)
{
    struct bsmp_server *server = (struct bsmp_server*) malloc(sizeof(*server));

    if(!server)
        return NULL;

    server_init(server);

    return server;
}

enum bsmp_err bsmp_server_destroy (bsmp_server_t* server)
{
    if(!server)
        return BSMP_ERR_PARAM_INVALID;

    free(server);

    return BSMP_SUCCESS;
}

#define SERVER_REGISTER(elem, max) \
    do {\
        if(!server) return BSMP_ERR_PARAM_INVALID;\
        enum bsmp_err err;\
        if((err = elem##_check(elem))) return err;\
        if(server->elem##s.count >= max) return BSMP_ERR_OUT_OF_MEMORY;\
        unsigned int i;\
        for(i = 0; i < server->elem##s.count; ++i)\
            if(server->elem##s.list[i] == elem)\
                return BSMP_ERR_DUPLICATE;\
        server->elem##s.list[server->elem##s.count] = elem;\
        elem->info.id = server->elem##s.count++;\
    }while(0)

enum bsmp_err bsmp_register_variable (bsmp_server_t *server,
                                      struct bsmp_var *var)
{
    SERVER_REGISTER(var, BSMP_MAX_VARIABLES);

    // Add to the group containing all variables
    group_add_var(&server->groups.list[GROUP_ALL_ID], var);

    // Add either to the WRITABLE or to the READ_ONLY group
    if(var->info.writable)
        group_add_var(&server->groups.list[GROUP_WRITE_ID], var);
    else
        group_add_var(&server->groups.list[GROUP_READ_ID], var);

    return BSMP_SUCCESS;
}

enum bsmp_err bsmp_register_curve (bsmp_server_t *server,
                                   struct bsmp_curve *curve)
{
    SERVER_REGISTER(curve, BSMP_MAX_CURVES);
    return BSMP_SUCCESS;
}

enum bsmp_err bsmp_register_function (bsmp_server_t *server,
                                      struct bsmp_func *func)
{
    SERVER_REGISTER(func, BSMP_MAX_FUNCTIONS);
    return BSMP_SUCCESS;
}

enum bsmp_err bsmp_register_hook(bsmp_server_t* server, bsmp_hook_t hook)
{
    if(!server || !hook)
        return BSMP_ERR_PARAM_INVALID;

    server->hook = hook;

    return BSMP_SUCCESS;
}

struct raw_message
{
    uint8_t command_code;
    uint8_t size[2];
    uint8_t payload[];
};///__attribute__((packed)); --> byte aligned

enum bsmp_err bsmp_process_packet (bsmp_server_t *server,
                                    struct bsmp_raw_packet *request,
                                    struct bsmp_raw_packet *response)
{
    if(!server || !request || !response)
        return BSMP_ERR_PARAM_INVALID;

    // Interpret packet payload as a message
    struct raw_message *recv_raw_msg = (struct raw_message *) request->data;
    struct raw_message *send_raw_msg = (struct raw_message *) response->data;

    // Create proper messages from the raw messages
    struct message recv_msg, send_msg;

    recv_msg.command_code = (enum command_code) recv_raw_msg->command_code;
    recv_msg.payload      = recv_raw_msg->payload;
    recv_msg.payload_size = (recv_raw_msg->size[0] << 8)+recv_raw_msg->size[1];

    send_msg.payload = send_raw_msg->payload;

    server->modified_list[0] = NULL;

    // Check inconsistency between the size of the received data and the size
    // specified in the message header
    if(request->len < BSMP_HEADER_SIZE ||
       request->len != recv_msg.payload_size + BSMP_HEADER_SIZE)
        MESSAGE_SET_ANSWER(&send_msg, CMD_ERR_MALFORMED_MESSAGE);
    // Check existence of the requested command
    else
    {
        switch(recv_msg.command_code)
        {

#define CASE_ITEM(cmd,func) case cmd: func(server,&recv_msg,&send_msg); break
            CASE_ITEM(CMD_QUERY_VERSION,        query_version);
            CASE_ITEM(CMD_VAR_QUERY_LIST,       var_query_list);
            CASE_ITEM(CMD_GROUP_QUERY_LIST,     group_query_list);
            CASE_ITEM(CMD_GROUP_QUERY,          group_query);
            CASE_ITEM(CMD_CURVE_QUERY_LIST,     curve_query_list);
            CASE_ITEM(CMD_FUNC_QUERY_LIST,      func_query_list);
            CASE_ITEM(CMD_VAR_READ,             var_read);
            CASE_ITEM(CMD_GROUP_READ,           group_read);
            CASE_ITEM(CMD_VAR_WRITE,            var_write);
            CASE_ITEM(CMD_GROUP_WRITE,          group_write);
            CASE_ITEM(CMD_VAR_WRITE_READ,       var_write_read);
            CASE_ITEM(CMD_GROUP_CREATE,         group_create);
            CASE_ITEM(CMD_GROUP_REMOVE_ALL,     group_remove_all);
#undef CASE_ITEM

        default:  
            MESSAGE_SET_ANSWER(&send_msg, CMD_ERR_OP_NOT_SUPPORTED);
            break;
        }
    }

    send_raw_msg->command_code = send_msg.command_code;

    send_raw_msg->size[0] = send_msg.payload_size >> 8;
    send_raw_msg->size[1] = send_msg.payload_size;

    response->len = send_msg.payload_size + BSMP_HEADER_SIZE;

    return BSMP_SUCCESS;
}
