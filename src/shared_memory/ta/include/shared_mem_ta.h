#ifndef __SHARED_MEM_TA_H__
#define __SHARED_MEM_TA_H__



/* UUID of the Shared Memory Trusted Application */
#define TA_SHARED_MEM_UUID \
	{ 0xa915515d, 0x0b93, 0x4291, \
		{ 0x97, 0xab, 0x2a, 0x3a, 0xab, 0x02, 0xb9, 0x21 } }

/*
 * TA_CMD_INIT_QUEUE - Initialize secure queue structure
 * param[0] (memref) Shared memory reference (input/output)
 * param[1] (value)  a: queue capacity in elements
 * param[2] (value)  a: element size in bytes
 * param[3] unused
 */
#define TA_CMD_INIT_QUEUE		0

/*
 * TA_CMD_ENQUEUE - Add element to secure queue
 * param[0] (memref) Input buffer (plaintext data)
 * param[1] (value)  a: data length (must <= element size)
 * param[2] (value)  a: encryption flags (TA_ENC_FLAG_*)
 * param[3] unused
 */
#define TA_CMD_ENQUEUE			1

/* Encryption flags */
#define TA_ENC_FLAG_NONE		0
#define TA_ENC_FLAG_AES_CTR		1

/*
 * TA_CMD_DEQUEUE - Remove element from secure queue
 * param[0] (memref) Output buffer (decrypted data)
 * param[1] (value)  a: buffer size (output: actual data length)
 * param[2] (value)  a: decryption flags (TA_ENC_FLAG_*)
 * param[3] unused
 */
#define TA_CMD_DEQUEUE			2

#endif /* __SHARED_MEM_TA_H__ */