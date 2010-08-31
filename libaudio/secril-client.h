/**
 * @file	secril-client.h
 *
 * @author	Myeongcheol Kim (mcmount.kim@samsung.com)
 *
 * @brief	RIL client library for multi-client support
 */

#ifndef __SECRIL_CLIENT_H__
#define __SECRIL_CLIENT_H__

#include <sys/types.h>


#ifdef __cplusplus
extern "C"
{
#endif 

struct RilClient
{
	void *prv;
};

typedef struct RilClient * HRilClient;


//---------------------------------------------------------------------------
// Defines
//---------------------------------------------------------------------------
#define RIL_CLIENT_ERR_SUCCESS		0
#define RIL_CLIENT_ERR_AGAIN		1
#define RIL_CLIENT_ERR_INIT			2	// Client is not initialized
#define RIL_CLIENT_ERR_INVAL		3	// Invalid value
#define RIL_CLIENT_ERR_CONNECT		4	// Connection error
#define RIL_CLIENT_ERR_IO			5	// IO error
#define RIL_CLIENT_ERR_RESOURCE		6	// Resource not available
#define RIL_CLIENT_ERR_UNKNOWN		7


//---------------------------------------------------------------------------
// Type definitions
//---------------------------------------------------------------------------

typedef int (*RilOnComplete)(HRilClient handle, const void *data, size_t datalen);

typedef int (*RilOnUnsolicited)(HRilClient handle, const void *data, size_t datalen);

typedef int (*RilOnError)(void *data, int error);


//---------------------------------------------------------------------------
// Client APIs
//---------------------------------------------------------------------------

/**
 * Open RILD multi-client.
 * Return is client handle, NULL on error.
 */
HRilClient OpenClient_RILD(void);

/**
 * Stop RILD multi-client. If client socket was connected,
 * it will be disconnected.
 */
int CloseClient_RILD(HRilClient client);

/**
 * Connect to RIL deamon. One client task starts.
 * Return is 0 or error code.
 */
int Connect_RILD(HRilClient client);

/**
 * check whether RILD is connected
 * Returns 0 or 1
 */
int isConnected_RILD(HRilClient client);

/**
 * Disconnect connection to RIL deamon(socket close).
 * Return is 0 or error code.
 */
int Disconnect_RILD(HRilClient client);

/**
 * Register unsolicited response handler. If handler is NULL,
 * the handler for the request ID is unregistered.
 * The response handler is invoked in the client task context.
 * Return is 0 or error code.
 */
int RegisterUnsolicitedHandler(HRilClient client, uint32_t id, RilOnUnsolicited handler);

/**
 * Register solicited response handler. If handler is NULL,
 * the handler for the ID is unregistered.
 * The response handler is invoked in the client task context.
 * Return is 0 or error code.
 */
int RegisterRequestCompleteHandler(HRilClient client, uint32_t id, RilOnComplete handler);


/**
 * Register error callback. If handler is NULL,
 * the callback is unregistered.
 * The response handler is invoked in the client task context.
 * Return is 0 or error code.
 */
int RegisterErrorCallback(HRilClient client, RilOnError cb, void *data);

/**
 * Invoke OEM request. Request ID is RIL_REQUEST_OEM_HOOK_RAW.
 * Return is 0 or error code. For RIL_CLIENT_ERR_AGAIN caller should retry.
 */
int InvokeOemRequestHookRaw(HRilClient client, char *data, size_t len);

#ifdef __cplusplus
};
#endif

#endif // __SECRIL_CLIENT_H__

// end of file

