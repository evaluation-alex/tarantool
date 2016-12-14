#include "vinyl.h"

#include <msgpuck.h>

#include "box.h" /* boxk() */
#include "cluster.h" /* SERVER_UUID */
#include "diag.h"
#include "iproto_constants.h"
#include "key_def.h"
#include "schema.h" /* BOX_VINYL_ID */
#include "tt_uuid.h"
#include "tuple.h"
#include "txn.h" /* box_txn_alloc() */

/*
 * Insert a run record into the vinyl metadata table.
 *
 * This function allocates a unique ID for the run on success.
 */
int
vy_meta_insert_run(const char *begin, const char *end,
		   const struct key_def *key_def,
		   enum vy_run_state state, int64_t *p_run_id)
{
	int64_t run_id = 0;
	char server_uuid_str[UUID_STR_LEN];
	tt_uuid_to_string(&SERVER_UUID, server_uuid_str);

	uint32_t key_size = 0;
	key_size += mp_sizeof_array(1);
	key_size += mp_sizeof_str(UUID_STR_LEN);

	char *key = box_txn_alloc(key_size);
	if (key == NULL) {
		diag_set(OutOfMemory, key_size, "box_txn_alloc", "key");
		return -1;
	}

	char *key_end = key;
	key_end = mp_encode_array(key_end, 1);
	key_end = mp_encode_str(key_end, server_uuid_str, UUID_STR_LEN);
	assert(key + key_size == key_end);

	box_tuple_t *max;
	if (box_index_max(BOX_VINYL_ID, 0, key, key_end, &max) != 0)
		return -1;
	if (max != NULL) {
		const char *data = tuple_data(max);
		(void)mp_decode_array(&data);
		uint32_t len;
		const char *str = mp_decode_str(&data, &len);
		struct tt_uuid uuid;
		if (tt_uuid_from_strl(str, len, &uuid) == 0 &&
		    tt_uuid_is_equal(&uuid, &SERVER_UUID))
			run_id = mp_decode_uint(&data) + 1;
	}

	char empty_key[16];
	assert(mp_sizeof_array(0) <= sizeof(empty_key));
	mp_encode_array(empty_key, 0);
	if (begin == NULL)
		begin = empty_key;
	if (end == NULL)
		end = empty_key;

	if (boxk(IPROTO_INSERT, BOX_VINYL_ID, "[%s%llu%u%u%llu%u%M%M]",
		 server_uuid_str, (unsigned long long)run_id,
		 (unsigned)key_def->space_id, (unsigned)key_def->iid,
		 (unsigned long long)key_def->opts.lsn, (unsigned)state,
		 begin, end) != 0)
		return -1;

	*p_run_id = run_id;
	return 0;
}

/*
 * Update the state of a run in the vinyl metadata table.
 */
int
vy_meta_update_run(int64_t run_id, enum vy_run_state state)
{
	assert(run_id >= 0);
	char server_uuid_str[UUID_STR_LEN];
	tt_uuid_to_string(&SERVER_UUID, server_uuid_str);
	return boxk(IPROTO_UPDATE, BOX_VINYL_ID, "[%s%llu][[%s%d%u]]",
		    server_uuid_str, (unsigned long long)run_id,
		    "=", 5, (unsigned)state);
}

/*
 * Delete a run record from the vinyl metadata table.
 */
int
vy_meta_delete_run(int64_t run_id)
{
	assert(run_id >= 0);
	char server_uuid_str[UUID_STR_LEN];
	tt_uuid_to_string(&SERVER_UUID, server_uuid_str);
	return boxk(IPROTO_DELETE, BOX_VINYL_ID, "[%s%llu]",
		    server_uuid_str, (unsigned long long)run_id);
}

