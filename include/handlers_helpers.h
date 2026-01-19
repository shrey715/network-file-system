/**
 * handlers_helpers.h - Handler utility functions
 *
 * Reusable functions for request handling in the name server.
 */

#ifndef HANDLERS_HELPERS_H
#define HANDLERS_HELPERS_H

#include "common.h"
#include "name_server.h"

extern NameServerState ns_state;

/**
 * SSConnection - Storage server connection info
 */
typedef struct {
  int socket;
  int ss_id;
  char ip[MAX_IP];
  int client_port;
} SSConnection;

/**
 * Send an error response to the client.
 */
static inline void send_error(int fd, MessageHeader *h, int err) {
  h->msg_type = MSG_ERROR;
  h->error_code = err;
  h->data_length = 0;
  send_message(fd, h, NULL);
}

/**
 * Send an ACK response to the client.
 */
static inline void send_ack(int fd, MessageHeader *h) {
  h->msg_type = MSG_ACK;
  h->error_code = ERR_SUCCESS;
  h->data_length = 0;
  send_message(fd, h, NULL);
}

/**
 * Find file and verify permission.
 *
 * @param filename    File to find
 * @param username    User requesting access
 * @param need_write  Non-zero if write permission required
 * @param file_out    Output file metadata pointer
 * @return ERR_SUCCESS or error code
 */
static inline int get_file_with_perm(const char *filename, const char *username,
                                     int need_write, FileMetadata **file_out) {
  FileMetadata *file = nm_find_file(filename);
  if (!file) {
    return ERR_FILE_NOT_FOUND;
  }

  int result = nm_check_permission(filename, username, need_write);
  if (result != ERR_SUCCESS) {
    return result;
  }

  *file_out = file;
  return ERR_SUCCESS;
}

/**
 * Connect to the storage server hosting a file.
 *
 * @param file  File metadata
 * @param conn  Output connection info
 * @return ERR_SUCCESS or error code
 */
static inline int connect_to_ss(FileMetadata *file, SSConnection *conn) {
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

/**
 * Get storage server with failover support.
 *
 * First tries the primary SS. If inactive, checks for active replica.
 * Logs failover when it occurs.
 *
 * @param ss_id     Primary storage server ID
 * @param op_name   Operation name for logging
 * @param filename  Filename for logging
 * @return Pointer to active SS or NULL if unavailable
 */
static inline StorageServerInfo *
get_ss_with_failover(int ss_id, const char *op_name_str, const char *filename) {
  StorageServerInfo *primary_ss = NULL;

  // Find primary SS
  for (int i = 0; i < ns_state.ss_count; i++) {
    if (ns_state.storage_servers[i].server_id == ss_id) {
      primary_ss = &ns_state.storage_servers[i];
      break;
    }
  }

  if (!primary_ss)
    return NULL;

  // Primary is active - use it
  if (primary_ss->is_active) {
    return primary_ss;
  }

  // Try failover to replica
  if (primary_ss->replica_active) {
    for (int i = 0; i < ns_state.ss_count; i++) {
      if (ns_state.storage_servers[i].server_id == primary_ss->replica_id &&
          ns_state.storage_servers[i].is_active) {
        char alert[512];
        snprintf(alert, sizeof(alert),
                 "[FAILOVER] Redirecting '%s' for '%s' to Replica SS #%d "
                 "(Primary #%d DOWN)",
                 op_name_str, filename, ns_state.storage_servers[i].server_id,
                 ss_id);
        log_message("NM", "WARN", alert);
        return &ns_state.storage_servers[i];
      }
    }
  }

  return NULL; // No active SS available
}

/**
 * Forward a request to the storage server and relay response to client.
 *
 * Handles permission checking, SS connection, message forwarding, and cleanup.
 *
 * @param client_fd   Client socket
 * @param header      Request header (modified)
 * @param ss_op_code  Operation code to send to SS
 * @param need_write  Non-zero if write permission required
 * @return Error code from SS response
 */
static inline int forward_to_ss(int client_fd, MessageHeader *header,
                                int ss_op_code, int need_write) {
  FileMetadata *file = NULL;
  int result =
      get_file_with_perm(header->filename, header->username, need_write, &file);
  if (result != ERR_SUCCESS) {
    send_error(client_fd, header, result);
    return result;
  }

  SSConnection conn;
  result = connect_to_ss(file, &conn);
  if (result != ERR_SUCCESS) {
    send_error(client_fd, header, result);
    return result;
  }

  MessageHeader ss_header = *header;
  ss_header.op_code = ss_op_code;
  send_message(conn.socket, &ss_header, NULL);

  char *ss_payload = NULL;
  MessageHeader ss_response;
  recv_message(conn.socket, &ss_response, &ss_payload);
  close(conn.socket);

  send_message(client_fd, &ss_response, ss_payload);
  if (ss_payload)
    free(ss_payload);

  return ss_response.error_code;
}

/**
 * Get human-readable operation name.
 */
static inline const char *op_name(int op) {
  switch (op) {
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

#endif /* HANDLERS_HELPERS_H */
