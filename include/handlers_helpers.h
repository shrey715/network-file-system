/*
 * handlers_helpers.h - Common helper functions for request handlers
 *
 * Provides reusable helper functions to reduce code duplication in handlers.c
 */

#ifndef HANDLERS_HELPERS_H
#define HANDLERS_HELPERS_H

#include "common.h"
#include "name_server.h"

// Forward declarations for external state
extern NameServerState ns_state;

/*
 * Helper structure for SS connection info
 */
typedef struct {
  int socket;
  int ss_id;
  char ip[MAX_IP];
  int client_port;
} SSConnection;

/*
 * send_error_response
 * @brief Send a simple error response to client
 *
 * @param client_fd Client socket
 * @param header Message header (will be modified)
 * @param error_code Error code to send
 */
static inline void send_error_response(int client_fd, MessageHeader *header,
                                       int error_code) {
  header->msg_type = MSG_ERROR;
  header->error_code = error_code;
  header->data_length = 0;
  send_message(client_fd, header, NULL);
}

/*
 * send_ack_response
 * @brief Send a simple ACK response to client
 *
 * @param client_fd Client socket
 * @param header Message header (will be modified)
 */
static inline void send_ack_response(int client_fd, MessageHeader *header) {
  header->msg_type = MSG_ACK;
  header->error_code = ERR_SUCCESS;
  header->data_length = 0;
  send_message(client_fd, header, NULL);
}

/*
 * find_file_with_permission
 * @brief Find file and check permission in one step
 *
 * @param filename File to find
 * @param username User requesting access
 * @param need_write Whether write permission is required
 * @param file_out Output pointer for file metadata
 * @return ERR_SUCCESS on success, or error code
 */
static inline int find_file_with_permission(const char *filename,
                                            const char *username,
                                            int need_write,
                                            FileMetadata **file_out) {
  FileMetadata *file = nm_find_file(filename);
  if (!file) {
    return ERR_FILE_NOT_FOUND;
  }

  int perm_result = nm_check_permission(filename, username, need_write);
  if (perm_result != ERR_SUCCESS) {
    return perm_result;
  }

  *file_out = file;
  return ERR_SUCCESS;
}

/*
 * connect_to_ss_for_file
 * @brief Connect to the storage server hosting a file
 *
 * @param file File metadata
 * @param conn Output connection info
 * @return ERR_SUCCESS on success, or error code
 */
static inline int connect_to_ss_for_file(FileMetadata *file,
                                         SSConnection *conn) {
  StorageServerInfo *ss = nm_find_storage_server(file->ss_id);
  if (!ss || !ss->is_active) {
    return ERR_SS_UNAVAILABLE;
  }

  conn->socket = connect_to_server(ss->ip, ss->client_port);
  if (conn->socket < 0) {
    return ERR_SS_UNAVAILABLE;
  }

  conn->ss_id = ss->server_id;
  strncpy(conn->ip, ss->ip, MAX_IP - 1);
  conn->ip[MAX_IP - 1] = '\0';
  conn->client_port = ss->client_port;

  return ERR_SUCCESS;
}

/*
 * forward_to_ss_simple
 * @brief Forward a request to SS and relay the response
 *
 * This handles the common pattern of:
 * 1. Check file exists and permissions
 * 2. Connect to SS
 * 3. Send request
 * 4. Relay response to client
 * 5. Cleanup
 *
 * @param client_fd Client socket
 * @param header Original client request header (modified)
 * @param ss_op_code Operation code to send to SS
 * @param need_write Whether write permission is needed
 * @return ERR_SUCCESS or error code
 */
static inline int forward_to_ss_simple(int client_fd, MessageHeader *header,
                                       int ss_op_code, int need_write) {
  FileMetadata *file = NULL;
  int result = find_file_with_permission(header->filename, header->username,
                                         need_write, &file);
  if (result != ERR_SUCCESS) {
    send_error_response(client_fd, header, result);
    return result;
  }

  SSConnection conn;
  result = connect_to_ss_for_file(file, &conn);
  if (result != ERR_SUCCESS) {
    send_error_response(client_fd, header, result);
    return result;
  }

  // Send request to SS
  MessageHeader ss_header = *header;
  ss_header.op_code = ss_op_code;
  send_message(conn.socket, &ss_header, NULL);

  // Receive and forward response
  char *ss_payload = NULL;
  MessageHeader ss_response;
  recv_message(conn.socket, &ss_response, &ss_payload);
  close(conn.socket);

  send_message(client_fd, &ss_response, ss_payload);

  if (ss_payload)
    free(ss_payload);

  return ss_response.error_code;
}

/*
 * get_operation_name
 * @brief Get human-readable operation name from op_code
 *
 * @param op_code Operation code
 * @return Static string with operation name
 */
static inline const char *get_operation_name(int op_code) {
  switch (op_code) {
  case OP_REGISTER_SS:
    return "SS_REGISTER";
  case OP_CONNECT_CLIENT:
    return "CLIENT_CONNECT";
  case OP_DISCONNECT:
    return "CLIENT_DISCONNECT";
  case OP_HEARTBEAT:
    return "HEARTBEAT";
  case OP_VIEW:
    return "VIEW";
  case OP_READ:
    return "READ";
  case OP_CREATE:
    return "CREATE";
  case OP_WRITE:
    return "WRITE";
  case OP_DELETE:
    return "DELETE";
  case OP_INFO:
    return "INFO";
  case OP_LIST:
    return "LIST";
  case OP_STREAM:
    return "STREAM";
  case OP_UNDO:
    return "UNDO";
  case OP_EXEC:
    return "EXEC";
  case OP_ADDACCESS:
    return "ADD_ACCESS";
  case OP_REMACCESS:
    return "REMOVE_ACCESS";
  case OP_MOVE:
    return "MOVE";
  case OP_CREATEFOLDER:
    return "CREATE_FOLDER";
  case OP_VIEWFOLDER:
    return "VIEW_FOLDER";
  case OP_CHECKPOINT:
    return "CHECKPOINT";
  case OP_VIEWCHECKPOINT:
    return "VIEW_CHECKPOINT";
  case OP_REVERT:
    return "REVERT";
  case OP_LISTCHECKPOINTS:
    return "LIST_CHECKPOINTS";
  case OP_REQUESTACCESS:
    return "REQUEST_ACCESS";
  case OP_VIEWREQUESTS:
    return "VIEW_REQUESTS";
  case OP_APPROVEREQUEST:
    return "APPROVE_REQUEST";
  case OP_DENYREQUEST:
    return "DENY_REQUEST";
  default:
    return "UNKNOWN";
  }
}

#endif // HANDLERS_HELPERS_H
