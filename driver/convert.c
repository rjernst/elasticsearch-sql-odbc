/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

#include <timestamp.h>

#include "log.h"

#define JSON_VAL_NULL			"null"
#define JSON_VAL_TRUE			"true"
#define JSON_VAL_FALSE			"false"

#define TM_TO_TIMESTAMP_STRUCT(_tmp/*src*/, _tsp/*dst*/) \
	do { \
		(_tsp)->year = (_tmp)->tm_year + 1900; \
		(_tsp)->month = (_tmp)->tm_mon + 1; \
		(_tsp)->day = (_tmp)->tm_mday; \
		(_tsp)->hour = (_tmp)->tm_hour; \
		(_tsp)->minute = (_tmp)->tm_min; \
		(_tsp)->second = (_tmp)->tm_sec; \
	} while (0)

/* For fixed size (destination) types, the target buffer can't be NULL. */
#define REJECT_IF_NULL_DEST_BUFF(_s/*tatement*/, _p/*ointer*/) \
	do { \
		if (! _p) { \
			ERRH(_s, "destination buffer can't be NULL."); \
			RET_HDIAGS(stmt, SQL_STATE_HY009); \
		} \
	} while (0)
#define REJECT_AS_OOR(_stmt, _val, _fix_val, _target) /* Out Of Range */ \
	do { \
		if (_fix_val) { \
			ERRH(_stmt, "can't convert value %llx to %s: out of range.", \
				_val, STR(_target)); \
		} else { \
			ERRH(_stmt, "can't convert value %f to %s: out of range.", \
				_val, STR(_target)); \
		} \
		RET_HDIAGS(_stmt, SQL_STATE_22003); \
	} while (0)


#if (0x0300 <= ODBCVER)
#	define ESSQL_TYPE_MIN		SQL_GUID
#	define ESSQL_TYPE_MAX		SQL_INTERVAL_MINUTE_TO_SECOND
#	define ESSQL_C_TYPE_MIN		SQL_C_UTINYINT
#	define ESSQL_C_TYPE_MAX		SQL_C_INTERVAL_MINUTE_TO_SECOND
#else /* ODBCVER < 0x0300 */
/* would need to adjust the limits  */
#	error "ODBC version not supported; must be 3.0 (0x0300) or higher"
#endif /* 0x0300 <= ODBCVER  */

#define ESSQL_NORM_RANGE		(ESSQL_TYPE_MAX - ESSQL_TYPE_MIN + 1)
#define ESSQL_C_NORM_RANGE		(ESSQL_C_TYPE_MAX - ESSQL_C_TYPE_MIN + 1)

/* conversion matrix SQL indexer */
#define	ESSQL_TYPE_IDX(_t)		(_t - ESSQL_TYPE_MIN)
/* conversion matrix C SQL indexer */
#define	ESSQL_C_TYPE_IDX(_t)	(_t - ESSQL_C_TYPE_MIN)

/* sparse SQL-C_SQL types conversion matrix, used for quick compatiblity check
 * on columns and parameters binding */
static BOOL compat_matrix[ESSQL_NORM_RANGE][ESSQL_C_NORM_RANGE] = {FALSE};

/* Note: check is array-access unsafe: types IDs must be validated prior to
 * checking compatibility (ex. meta type setting)  */
#define ESODBC_TYPES_COMPATIBLE(_sql, _csql) \
	/* if not within the ODBC range, it can only by a binary conversion;.. */ \
	((ESSQL_TYPE_MAX < _sql && _csql == SQL_C_BINARY) || \
		/* ..otheriwse use the conversion matrix */ \
		compat_matrix[ESSQL_TYPE_IDX(_sql)][ESSQL_C_TYPE_IDX(_csql)])

/* populates the compat_matrix as required in:
 * https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/converting-data-from-c-to-sql-data-types */
void convert_init()
{
	SQLSMALLINT i, j, sql, csql;
	/*INDENT-OFF*/
	SQLSMALLINT block_idx_sql[] = {SQL_CHAR, SQL_VARCHAR, SQL_LONGVARCHAR,
			SQL_WCHAR, SQL_WVARCHAR, SQL_WLONGVARCHAR, SQL_DECIMAL,
			SQL_NUMERIC, SQL_BIT, ESODBC_SQL_BOOLEAN, SQL_TINYINT,
			SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT, SQL_REAL, SQL_FLOAT,
			SQL_DOUBLE
		};
	/*INDENT-ON*/
	SQLSMALLINT block_idx_csql[] = {SQL_C_CHAR, SQL_C_WCHAR,
			SQL_C_BIT, SQL_C_NUMERIC, SQL_C_STINYINT, SQL_C_UTINYINT,
			SQL_C_TINYINT, SQL_C_SBIGINT, SQL_C_UBIGINT, SQL_C_SSHORT,
			SQL_C_USHORT, SQL_C_SHORT, SQL_C_SLONG, SQL_C_ULONG,
			SQL_C_LONG, SQL_C_FLOAT, SQL_C_DOUBLE, SQL_C_BINARY
		};
	SQLSMALLINT to_csql_interval[] = {SQL_CHAR, SQL_VARCHAR, SQL_LONGVARCHAR,
			SQL_WCHAR, SQL_WVARCHAR, SQL_WLONGVARCHAR, SQL_DECIMAL,
			SQL_NUMERIC, SQL_TINYINT, SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT
		};
	SQLSMALLINT from_sql_interval[] = {SQL_C_CHAR, SQL_C_WCHAR,
			SQL_C_BIT,SQL_C_NUMERIC, SQL_C_STINYINT, SQL_C_UTINYINT,
			SQL_C_TINYINT, SQL_C_SBIGINT, SQL_C_UBIGINT, SQL_C_SSHORT,
			SQL_C_USHORT, SQL_C_SHORT, SQL_C_SLONG, SQL_C_ULONG,
			SQL_C_LONG
		};
	SQLSMALLINT sql_interval[] = {SQL_INTERVAL_MONTH, SQL_INTERVAL_YEAR,
			SQL_INTERVAL_YEAR_TO_MONTH, SQL_INTERVAL_DAY,
			SQL_INTERVAL_HOUR, SQL_INTERVAL_MINUTE, SQL_INTERVAL_SECOND,
			SQL_INTERVAL_DAY_TO_HOUR, SQL_INTERVAL_DAY_TO_MINUTE,
			SQL_INTERVAL_DAY_TO_SECOND, SQL_INTERVAL_HOUR_TO_MINUTE,
			SQL_INTERVAL_HOUR_TO_SECOND, SQL_INTERVAL_MINUTE_TO_SECOND
		};
	SQLSMALLINT csql_interval[] = {SQL_C_INTERVAL_DAY, SQL_C_INTERVAL_HOUR,
			SQL_C_INTERVAL_MINUTE, SQL_C_INTERVAL_SECOND,
			SQL_C_INTERVAL_DAY_TO_HOUR, SQL_C_INTERVAL_DAY_TO_MINUTE,
			SQL_C_INTERVAL_DAY_TO_SECOND, SQL_C_INTERVAL_HOUR_TO_MINUTE,
			SQL_C_INTERVAL_HOUR_TO_SECOND,
			SQL_C_INTERVAL_MINUTE_TO_SECOND, SQL_C_INTERVAL_MONTH,
			SQL_C_INTERVAL_YEAR, SQL_C_INTERVAL_YEAR_TO_MONTH
		};
	SQLSMALLINT to_csql_datetime[] = {SQL_CHAR, SQL_VARCHAR, SQL_LONGVARCHAR,
			SQL_WCHAR, SQL_WVARCHAR, SQL_WLONGVARCHAR, SQL_TYPE_DATE,
			SQL_TYPE_TIME, SQL_TYPE_TIMESTAMP
		};
	SQLSMALLINT csql_datetime[] = {SQL_C_TYPE_DATE, SQL_C_TYPE_TIME,
			SQL_C_TYPE_TIMESTAMP
		};

	/* fill the compact block of TRUEs (growing from the upper left corner) */
	for (i = 0; i < sizeof(block_idx_sql)/sizeof(*block_idx_sql) ; i ++) {
		for (j = 0; j < sizeof(block_idx_csql)/sizeof(*block_idx_csql) ; j ++) {
			sql = block_idx_sql[i];
			csql = block_idx_csql[j];
			compat_matrix[ESSQL_TYPE_IDX(sql)][ESSQL_C_TYPE_IDX(csql)] = TRUE;
		}
	}

	/* SQL_C_ BINARY, CHAR, WCHAR and DEFAULT are comatible with all SQL types;
	 * this will set also non-ODBC intersections (but it's convenient) */
	for (sql = 0; sql < ESSQL_NORM_RANGE; sql ++) {
		compat_matrix[sql][ESSQL_C_TYPE_IDX(SQL_C_CHAR)] = TRUE;
		compat_matrix[sql][ESSQL_C_TYPE_IDX(SQL_C_WCHAR)] = TRUE;
		compat_matrix[sql][ESSQL_C_TYPE_IDX(SQL_C_BINARY)] = TRUE;
		compat_matrix[sql][ESSQL_C_TYPE_IDX(SQL_C_DEFAULT)] = TRUE;
	}

	/* ESODBC_SQL_NULL (NULL) is compabitle with all SQL_C types */
	for (csql = 0; csql < ESSQL_C_NORM_RANGE; csql ++) {
		compat_matrix[ESSQL_TYPE_IDX(ESODBC_SQL_NULL)][csql] = TRUE;
	}

	/* set conversions to INTERVAL_C */
	for (i = 0; i < sizeof(to_csql_interval)/sizeof(*to_csql_interval); i ++) {
		for (j = 0; j < sizeof(csql_interval)/sizeof(*csql_interval); j ++ ) {
			sql = to_csql_interval[i];
			csql = csql_interval[j];
			compat_matrix[ESSQL_TYPE_IDX(sql)][ESSQL_C_TYPE_IDX(csql)] = TRUE;
		}
	}

	/* set conversions from INTERVAL_SQL */
	for (i = 0; i < sizeof(sql_interval)/sizeof(*sql_interval); i ++) {
		for (j = 0; j < sizeof(from_sql_interval)/sizeof(*from_sql_interval);
			j ++ ) {
			sql = sql_interval[i];
			csql = from_sql_interval[j];
			compat_matrix[ESSQL_TYPE_IDX(sql)][ESSQL_C_TYPE_IDX(csql)] = TRUE;
		}
	}

	/* set conversions between date-time types */
	for (i = 0; i < sizeof(to_csql_datetime)/sizeof(*to_csql_datetime); i ++) {
		for (j = 0; j < sizeof(csql_datetime)/sizeof(*csql_datetime); j ++ ) {
			sql = to_csql_datetime[i];
			csql = csql_datetime[j];
			if (sql == SQL_TYPE_DATE && csql == SQL_C_TYPE_TIME) {
				continue;
			}
			if (sql == SQL_TYPE_TIME && csql == SQL_C_TYPE_DATE) {
				continue;
			}
			compat_matrix[ESSQL_TYPE_IDX(sql)][ESSQL_C_TYPE_IDX(csql)] = TRUE;
		}
	}

	/* GUID conversion */
	sql = SQL_GUID;
	csql = SQL_C_GUID;
	compat_matrix[ESSQL_TYPE_IDX(sql)][ESSQL_C_TYPE_IDX(csql)] = TRUE;
}


SQLRETURN set_param_decdigits(esodbc_rec_st *irec,
	SQLUSMALLINT param_no, SQLSMALLINT decdigits)
{
	assert(irec->desc->type == DESC_TYPE_IPD);

	switch (irec->meta_type) {
		/* for "SQL_TYPE_TIME, SQL_TYPE_TIMESTAMP, SQL_INTERVAL_SECOND,
		 * SQL_INTERVAL_DAY_TO_SECOND, SQL_INTERVAL_HOUR_TO_SECOND, or
		 * SQL_INTERVAL_MINUTE_TO_SECOND, the SQL_DESC_PRECISION field of the
		 * IPD is set to DecimalDigits." */
		case METATYPE_DATETIME:
			if (irec->concise_type == SQL_TYPE_DATE) {
				break;
			}
		case METATYPE_INTERVAL_WSEC:
			if (decdigits < 0) {
				ERRH(irec->desc, "can't set negative (%hd) as fractional "
					"second precision.", decdigits);
				RET_HDIAGS(irec->desc, SQL_STATE_HY104);
			}
			return EsSQLSetDescFieldW(irec->desc, param_no, SQL_DESC_PRECISION,
					(SQLPOINTER)(intptr_t)decdigits, SQL_IS_SMALLINT);

		/* for " SQL_NUMERIC or SQL_DECIMAL, the SQL_DESC_SCALE field of the
		 * IPD is set to DecimalDigits." */
		case METATYPE_EXACT_NUMERIC:
			if (irec->concise_type == SQL_DECIMAL ||
				irec->concise_type == SQL_NUMERIC) {
				return EsSQLSetDescFieldW(irec->desc, param_no, SQL_DESC_SCALE,
						(SQLPOINTER)(intptr_t)decdigits, SQL_IS_SMALLINT);
			}
			break; // formal

		default:
			/* "For all other data types, the DecimalDigits argument is
			 * ignored." */
			;
	}

	return SQL_SUCCESS;
}

SQLSMALLINT get_param_decdigits(esodbc_rec_st *irec)
{
	assert(irec->desc->type == DESC_TYPE_IPD);

	switch(irec->meta_type) {
		case METATYPE_DATETIME:
			if (irec->concise_type == SQL_TYPE_DATE) {
				break;
			}
		case METATYPE_INTERVAL_WSEC:
			return irec->precision;

		case METATYPE_EXACT_NUMERIC:
			if (irec->concise_type == SQL_DECIMAL ||
				irec->concise_type == SQL_NUMERIC) {
				return irec->scale;
			}
			break;

		default:
			WARNH(irec->desc, "retriving decdigits for IPD metatype: %d.",
				irec->meta_type);
	}

	return 0;
}

SQLRETURN set_param_size(esodbc_rec_st *irec,
	SQLUSMALLINT param_no, SQLULEN size)
{
	assert(irec->desc->type == DESC_TYPE_IPD);

	switch (irec->meta_type) {
		/* for "SQL_CHAR, SQL_VARCHAR, SQL_LONGVARCHAR, SQL_BINARY,
		 * SQL_VARBINARY, SQL_LONGVARBINARY, or one of the concise SQL
		 * datetime or interval data types, the SQL_DESC_LENGTH field of the
		 * IPD is set to the value of [s]ize." */
		case METATYPE_STRING:
		case METATYPE_BIN:
		case METATYPE_DATETIME:
		case METATYPE_INTERVAL_WSEC:
		case METATYPE_INTERVAL_WOSEC:
			return EsSQLSetDescFieldW(irec->desc, param_no, SQL_DESC_LENGTH,
					(SQLPOINTER)(uintptr_t)size, SQL_IS_UINTEGER);

		/* for "SQL_DECIMAL, SQL_NUMERIC, SQL_FLOAT, SQL_REAL, or SQL_DOUBLE,
		 * the SQL_DESC_PRECISION field of the IPD is set to the value of
		 * [s]ize." */
		case METATYPE_EXACT_NUMERIC:
			if (irec->concise_type != SQL_DECIMAL &&
				irec->concise_type != SQL_NUMERIC) {
				break;
			}
		/* no break */
		case METATYPE_FLOAT_NUMERIC:
			// TODO: https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/column-size :
			// "The ColumnSize argument of SQLBindParameter is ignored for
			// this data type." (floats included): ????
			return EsSQLSetDescFieldW(irec->desc, param_no, SQL_DESC_PRECISION,
					/* cast: ULEN -> SMALLINT; XXX: range check? */
					(SQLPOINTER)(uintptr_t)size, SQL_IS_SMALLINT);

		default:
			;/* "For other data types, the [s]ize argument is ignored." */
	}
	return SQL_SUCCESS;
}

SQLULEN get_param_size(esodbc_rec_st *irec)
{
	assert(irec->desc->type == DESC_TYPE_IPD);

	switch (irec->meta_type) {
		case METATYPE_STRING:
		case METATYPE_BIN:
		case METATYPE_DATETIME:
		case METATYPE_INTERVAL_WSEC:
		case METATYPE_INTERVAL_WOSEC:
			return irec->length;

		case METATYPE_BIT:
		case METATYPE_EXACT_NUMERIC:
			// TODO: make DEC, NUM a floating meta?
			if (irec->concise_type != SQL_DECIMAL &&
				irec->concise_type != SQL_NUMERIC) {
				assert(irec->es_type);
				return irec->es_type->column_size;
			}

		case METATYPE_FLOAT_NUMERIC:
			return irec->precision;

		default:
			WARNH(irec->desc, "retriving colsize for IPD metatype: %d.",
				irec->meta_type);
	}
	return 0;
}


/*
 * field: SQL_DESC_: DATA_PTR / INDICATOR_PTR / OCTET_LENGTH_PTR
 * pos: position in array/row_set (not result_set)
 */
inline void *deferred_address(SQLSMALLINT field_id, size_t pos,
	esodbc_rec_st *rec)
{
	size_t elem_size;
	SQLLEN offt;
	void *base;
	esodbc_desc_st *desc = rec->desc;

#define ROW_OFFSETS \
	do { \
		elem_size = desc->bind_type; \
		offt = desc->bind_offset_ptr ? *(desc->bind_offset_ptr) : 0; \
	} while (0)

	switch (field_id) {
		case SQL_DESC_DATA_PTR:
			base = rec->data_ptr;
			if (desc->bind_type == SQL_BIND_BY_COLUMN) {
				elem_size = (size_t)rec->octet_length;
				offt = 0;
			} else { /* by row */
				ROW_OFFSETS;
			}
			break;
		case SQL_DESC_INDICATOR_PTR:
			base = rec->indicator_ptr;
			if (desc->bind_type == SQL_BIND_BY_COLUMN) {
				elem_size = sizeof(*rec->indicator_ptr);
				offt = 0;
			} else { /* by row */
				ROW_OFFSETS;
			}
			break;
		case SQL_DESC_OCTET_LENGTH_PTR:
			base = rec->octet_length_ptr;
			if (desc->bind_type == SQL_BIND_BY_COLUMN) {
				elem_size = sizeof(*rec->octet_length_ptr);
				offt = 0;
			} else { /* by row */
				ROW_OFFSETS;
			}
			break;
		default:
			BUG("can't calculate the deferred address of field type %d.",
				field_id);
			return NULL;
	}
#undef ROW_OFFSETS

	DBGH(desc->hdr.stmt, "rec@0x%p, field_id:%hd, pos: %zu : base@0x%p, "
		"offset=%lld, elem_size=%zu", rec, field_id, pos, base, offt,
		elem_size);

	return base ? (char *)base + offt + pos * elem_size : NULL;
}

/*
 * Handles the lengths of the data to copy out to the application:
 * (1) returns the max amount of bytes to copy (in the data_ptr), taking into
 *     account size of data and of buffer, relevant statement attribute and
 *     buffer type;
 * (2) indicates if truncation occured into 'state'.
 * WARN: only to be used with ARD.meta_type == STR || BIN (as it can indicate
 * a size to copy smaller than the original -- truncating).
 */
static size_t buff_octet_size(
	size_t avail, /* how many bytes are there to copy out */
	size_t unit_size, /* the unit size of the buffer (i.e. sizeof(wchar_t)) */
	esodbc_rec_st *arec, esodbc_rec_st *irec,
	esodbc_state_et *state /* out param: only written when truncating */)
{
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	/* how large (bytes) is the buffer to copy into*/
	size_t room = (size_t)arec->octet_length;
	/* statement attribute SQL_ATTR_MAX_LENGTH value */
	size_t attr_max = stmt->max_length;
	/* meta type of IRD */
	esodbc_metatype_et ird_mt = irec->meta_type;
	size_t max_copy, max;

	/* type is signed, driver should not allow a negative to this point:
	 * making sure the cast above is sane. */
	assert(0 <= arec->octet_length);

	/* truncate to statment max bytes, only if "the column contains character
	 * or binary data" */
	max = (ird_mt == METATYPE_STRING || ird_mt == METATYPE_BIN) ? attr_max : 0;

	/* apply "network" truncation first, if need to */
	if (0 < max && max < avail) {
		INFO("applying 'network' truncation %zd -> %zd.", avail, max);
		max_copy = max;
		/* no truncation indicated for this case */
	} else {
		max_copy = avail;
	}

	/* is target buffer to small? adjust size if so and indicate truncation */
	/* Note: this should only be tested/applied if ARD.meta_type == STR||BIN */
	// FIXME: check note above
	if (room < max_copy) {
		INFO("applying buffer truncation %zd -> %zd.", max_copy, room);
		max_copy = room;
		*state = SQL_STATE_01004;
	}

	/* adjust to align to target buffer unit */
	if (max_copy % unit_size) {
		max_copy -= max_copy % unit_size;
	}

	DBG("avail=%zd, room=%zd, attr_max=%zd, metatype:%d => "
		"max_copy=%zd, state=%d.",
		avail, room, attr_max, ird_mt, max_copy, *state);
	return max_copy;
}

/*
 * Indicate the amount of data available to the application, taking into
 * account: the type of data, should truncation - due to max length attr
 * setting - need to be indicated, since original length is indicated, w/o
 * possible buffer truncation, but with possible 'network' truncation.
 */
static inline void write_out_octets(
	SQLLEN *octet_len_ptr, /* buffer to write the avail octets into */
	size_t avail, /* amount of bytes avail */
	esodbc_rec_st *irec)
{
	esodbc_stmt_st *stmt = irec->desc->hdr.stmt;
	/* statement attribute SQL_ATTR_MAX_LENGTH value */
	size_t attr_max = stmt->max_length;
	/* meta type of IRD */
	esodbc_metatype_et ird_mt = irec->meta_type;
	size_t max;

	if (! octet_len_ptr) {
		DBG("NULL octet len pointer, length (%zd) not indicated.", avail);
		return;
	}

	/* truncate to statment max bytes, only if "the column contains character
	 * or binary data" */
	max = (ird_mt == METATYPE_STRING || ird_mt == METATYPE_BIN) ? attr_max : 0;

	if (0 < max) {
		/* put the value of SQL_ATTR_MAX_LENGTH attribute..  even
		 * if this would be larger than what the data actually
		 * occupies after conversion: "the driver has no way of
		 * figuring out what the actual length is" */
		*octet_len_ptr = max;
		DBG("max length (%zd) attribute enforced.", max);
	} else {
		/* if no "network" truncation done, indicate data's length, no
		 * matter if truncated to buffer's size or not */
		*octet_len_ptr = avail;
	}

	DBG("length of data available for transfer: %ld", *octet_len_ptr);
}

/* if an application doesn't specify the conversion, use column's type */
static inline SQLSMALLINT get_rec_c_type(esodbc_rec_st *arec,
	esodbc_rec_st *irec)
{
	SQLSMALLINT ctype;
	/* "To use the default mapping, an application specifies the SQL_C_DEFAULT
	 * type identifier." */
	if (arec->concise_type != SQL_C_DEFAULT) {
		ctype = arec->concise_type;
	} else {
		ctype = irec->es_type->c_concise_type;
	}
	DBGH(arec->desc, "target data C type: %hd.", ctype);
	return ctype;
}


static inline void gd_offset_apply(esodbc_stmt_st *stmt, xstr_st *xstr)
{
	if (! STMT_GD_CALLING(stmt)) {
		return;
	}

	assert(0 <= stmt->gd_offt); /* negative means "data exhausted" */
	assert(stmt->gd_offt < (SQLLEN)xstr->w.cnt + /*\0*/1);

	if (xstr->wide) {
		xstr->w.str += stmt->gd_offt;
		xstr->w.cnt -= stmt->gd_offt;
	} else {
		xstr->c.str += stmt->gd_offt;
		xstr->c.cnt -= stmt->gd_offt;
	}

	DBGH(stmt, "applied an offset of %lld.", stmt->gd_offt);
}

/*
 * cnt: character count of string/bin to transfer, excluding \0
 * xfed: char count of copied data.
 */
static inline void gd_offset_update(esodbc_stmt_st *stmt, size_t cnt,
	size_t xfed)
{
	if (! STMT_GD_CALLING(stmt)) {
		return;
	}

	if (cnt <= xfed) {
		/* if all has been transfered, indicate so in the gd_offt */
		stmt->gd_offt = -1;
	} else {
		stmt->gd_offt += xfed;
	}

	DBGH(stmt, "offset updated with %zu to new value of %lld.", xfed,
		stmt->gd_offt);
}



/* transfer to the application a 0-terminated (but unaccounted for) cstr_st */
static SQLRETURN transfer_xstr0(esodbc_rec_st *arec, esodbc_rec_st *irec,
	xstr_st *xsrc, void *data_ptr, SQLLEN *octet_len_ptr)
{
	size_t in_bytes, in_chars;
	SQLCHAR *dst_c;
	SQLWCHAR *dst_w;
	size_t cnt, char_sz;
	esodbc_state_et state = SQL_STATE_00000;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	/* apply source offset, if this is a SQLGetData() call  */
	gd_offset_apply(stmt, xsrc);

	cnt = xsrc->w.cnt; /*==->c.cnt*/
	if (xsrc->wide) {
		/* the source string must be 0-term'd (buff_octet_size param below
		 * counts it) */
		assert(xsrc->w.str[cnt] == 0);
		char_sz = sizeof(*xsrc->w.str);
	} else {
		assert(xsrc->c.str[cnt] == 0);
		char_sz = sizeof(*xsrc->c.str);
	}

	/* always return the app the untruncated number of bytes */
	write_out_octets(octet_len_ptr, cnt * char_sz, irec);

	if (data_ptr) {
		in_bytes = buff_octet_size((cnt + /*\0*/1) * char_sz, char_sz,
				arec, irec, &state);
		if (in_bytes) {
			in_chars = in_bytes / char_sz;
			/* deduct the \0 added above; which is needed, since we need to
			 * copy it too out to the app (or truncate the data, but still not
			 * count the \0) */
			in_chars --;

			if (xsrc->wide) {
				dst_w = (SQLWCHAR *)data_ptr;
				memcpy(dst_w, xsrc->w.str, in_bytes);
				/* TODO: should the left be filled with spaces? :
				 * https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/rules-for-conversions */

				if (state != SQL_STATE_00000) {
					/* 0-term the buffer */
					dst_w[in_chars] = 0;
					DBGH(stmt, "aREC@0x%p: `" LWPDL "` transfered truncated "
						"as `" LWPDL "` at data_ptr@0x%p.", arec,
						LWSTR(&xsrc->w), in_chars, dst_w, dst_w);
				} else {
					assert(dst_w[in_chars] == 0);
					DBGH(stmt, "aREC@0x%p: `" LWPDL "` transfered at "
						"data_ptr@0x%p.", arec, LWSTR(&xsrc->w), dst_w);
				}
			} else {
				dst_c = (SQLCHAR *)data_ptr;
				memcpy(dst_c, xsrc->c.str, in_bytes);

				if (state != SQL_STATE_00000) {
					/* 0-term the buffer */
					dst_c[in_chars] = 0;
					DBGH(stmt, "aREC@0x%p: `" LCPDL "` transfered truncated "
						"as `" LCPDL "` at data_ptr@0x%p.", arec,
						LCSTR(&xsrc->w), in_chars, dst_c, dst_c);
				} else {
					assert(dst_c[in_chars] == 0);
					DBGH(stmt, "aREC@0x%p: `" LCPDL "` transfered at "
						"data_ptr@0x%p.", arec, LCSTR(&xsrc->c), dst_c);
				}
			}

			/* only update offset if data is copied out */
			gd_offset_update(stmt, xsrc->w.cnt, in_chars); /*==->c.cnt*/
		}
	} else {
		DBGH(stmt, "aREC@0x%p: NULL transfer buffer.", arec);
	}

	if (state != SQL_STATE_00000) {
		RET_HDIAGS(stmt, state);
	}
	return SQL_SUCCESS;
}

/* 10^n */
static inline unsigned long long pow10(unsigned n)
{
	unsigned long long pow = 1;
	pow <<= n;
	while (n--) {
		pow += pow << 2;
	}
	return pow;
}

static SQLRETURN double_to_numeric(esodbc_rec_st *arec, double src, void *dst)
{
	SQL_NUMERIC_STRUCT *numeric;
	esodbc_stmt_st *stmt;
	SQLSMALLINT prec/*..ision*/;
	unsigned long long ullng, pow;
	long long llng;

	stmt = arec->desc->hdr.stmt;
	numeric = (SQL_NUMERIC_STRUCT *)dst;
	assert(numeric);

	numeric->scale = (SQLCHAR)arec->scale;
	numeric->sign = 0 <= src;

	ullng = numeric->sign ? (unsigned long long)src : (unsigned long long)-src;
	/* =~ log10(abs(src)) */
	for (prec = 0; ullng; prec ++) {
		ullng /= 10;
	}
	if (arec->scale < 0) {
		pow = pow10(-arec->scale);
		llng = (long long)(src / pow);
		prec += arec->scale; /* prec lowers */
	} else if (0 < arec->scale) {
		pow = pow10(arec->scale);
		if (DBL_MAX / pow < src) {
			// TODO: numeric.val is 16 octets long -> expand
			ERRH(stmt, "max numeric conversion scale reached.");
			RET_HDIAGS(stmt, SQL_STATE_22003);
		}
		llng = (long long)(src * pow);
		prec += arec->scale; /* prec grows */
	} else {
		llng = (long long)src;
	}
	ullng = numeric->sign ? (unsigned long long)llng :
		(unsigned long long)-llng;

	DBGH(stmt, "arec@0x%p: precision=%hd, scale=%hd; src.precision=%hd",
		arec, arec->precision, arec->scale, prec);
	if ((UCHAR_MAX < prec) || (0 < arec->precision && arec->precision < prec)) {
		/* precision of source is higher than requested => overflow */
		ERRH(stmt, "conversion overflow. source: %.6e; requested: "
			"precisions: %d, scale: %d.", src, arec->precision, arec->scale);
		RET_HDIAGS(stmt, SQL_STATE_22003);
	} else if (prec < 0) {
		prec = 0;
		assert(ullng == 0);
	}
	numeric->precision = (SQLCHAR)prec;


#if REG_DWORD != REG_DWORD_LITTLE_ENDIAN
	ullng = _byteswap_ulong(ullng);
#endif /* LE */
	assert(sizeof(ullng) <= sizeof(numeric->val));
	memcpy(numeric->val, (char *)&ullng, sizeof(ullng));
	memset(numeric->val+sizeof(ullng), 0, sizeof(numeric->val)-sizeof(ullng));

	DBGH(stmt, "double %.6e converted to numeric: .sign=%d, precision=%d "
		"(req: %d), .scale=%d (req: %d), .val:`" LCPDL "` (0x%lx).", src,
		numeric->sign, numeric->precision, arec->precision,
		numeric->scale, arec->scale, (int)sizeof(numeric->val), numeric->val,
		ullng);
	return SQL_SUCCESS;
}

static SQLRETURN numeric_to_double(esodbc_rec_st *irec, void *src, double *dst)
{
	unsigned long long ullng, pow;
	double dbl;
	SQLSMALLINT prec/*..ision*/;
	SQL_NUMERIC_STRUCT *numeric;
	esodbc_stmt_st *stmt = irec->desc->hdr.stmt;

	assert(src);
	numeric = (SQL_NUMERIC_STRUCT *)src;

	assert(2 * sizeof(ullng) == sizeof(numeric->val));
	ullng = *(unsigned long long *)&numeric->val[sizeof(ullng)];
	if (ullng) {
		// TODO: shift down with scale
		ERRH(stmt, "max numeric precision scale reached.");
		goto erange;
	}
	ullng = *(unsigned long long *)&numeric->val[0];
#if REG_DWORD != REG_DWORD_LITTLE_ENDIAN
	ullng = _byteswap_ulong(ullng);
#endif /* LE */

	/* =~ log10(abs(ullng)) */
	for (prec = 0, pow = ullng; pow; prec ++) {
		pow /= 10;
	}

	if (DBL_MAX < ullng) {
		goto erange;
	} else {
		dbl = (double)ullng;
	}

	if (numeric->scale < 0) {
		pow = pow10(-numeric->scale);
		if (DBL_MAX / pow < dbl) {
			goto erange;
		}
		dbl *= pow;
		prec -= numeric->scale; /* prec grows */
	} else if (0 < numeric->scale) {
		pow = pow10(numeric->scale);
		dbl /= pow;
		prec -= numeric->scale; /* prec lowers */
	}

	DBGH(stmt, "irec@0x%p: precision=%hd, scale=%hd; src.precision=%hd",
		irec, irec->precision, irec->scale, prec);
	if ((UCHAR_MAX < prec) || (0 < irec->precision && irec->precision < prec)) {
		ERRH(stmt, "source precision (%hd) larger than requested (%hd)",
			prec, irec->precision);
		goto erange;
	} else {
		if (! numeric->sign) {
			dbl = -dbl;
		}
	}

	DBGH(stmt, "VAL: %f", dbl);
	DBGH(stmt, "numeric val: %llu, scale: %hhd, precision: %hhu converted to "
		"double %.6e.", ullng, numeric->scale, numeric->precision, dbl);

	*dst = dbl;
	return SQL_SUCCESS;
erange:
	ERRH(stmt, "can't convert numeric val: %llu, scale: %hhd, precision: %hhu"
		" to double.", ullng, numeric->scale, numeric->precision);
	RET_HDIAGS(stmt, SQL_STATE_22003);
}

/*
 * https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/transferring-data-in-its-binary-form
 */
static SQLRETURN llong_to_binary(esodbc_rec_st *arec, esodbc_rec_st *irec,
	long long src, void *dst, SQLLEN *src_len)
{
	size_t cnt;
	char *s = (char *)&src;
	esodbc_state_et state = SQL_STATE_00000;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	unsigned long long ull = src < 0 ? -src : src;

	/* UJ4C uses long long for any integer type -> find out the
	 * smallest type that would accomodate the value (since fixed negatives
	 * would take more space then minimally required). */
	if (ull < CHAR_MAX) {
		cnt = sizeof(char);
	} else if (ull < SHRT_MAX) {
		cnt = sizeof(short);
	} else if (ull < INT_MAX) {
		cnt = sizeof(int);
	} else if (ull < LONG_MAX) {
		cnt = sizeof(long);
	} else { /* definetely ull < LLONG_MAX */
		cnt = sizeof(long long);
	}

	cnt = buff_octet_size(cnt, sizeof(*s), arec, irec, &state);
	if (state) { /* has it been shrunk? */
		REJECT_AS_OOR(stmt, src, /*fixed?*/TRUE, "[BINARY]<[value]");
	}

	if (dst) {
		/* copy bytes as-are: the reverse conversion need to take place on
		 * "same DBMS and hardare platform". */
		memcpy(dst, s, cnt);
		//TODO: should the driver clear all the received buffer?? Cfg option?
		//memset((char *)dst + cnt, 0, arec->octet_length - cnt);
	}
	write_out_octets(src_len, cnt, irec);
	DBGH(stmt, "long long value %lld, converted on %zd octets.", src, cnt);

	return SQL_SUCCESS;
}

static SQLRETURN longlong_to_str(esodbc_rec_st *arec, esodbc_rec_st *irec,
	long long ll, void *data_ptr, SQLLEN *octet_len_ptr, BOOL wide)
{
	SQLRETURN ret;
	/* buffer is overprovisioned for !wide, but avoids double declaration */
	SQLCHAR buff[(ESODBC_PRECISION_INT64 + /*0-term*/1 + /*+/-*/1)
		* sizeof(SQLWCHAR)];
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	xstr_st xsrc = (xstr_st) {
		.wide = wide,
		.c = (cstr_st) {
			buff, 0
		}
	};

	xsrc.w.cnt = i64tot((int64_t)ll, buff, wide); /*==.c.cnt*/

	if (wide) {
		DBGH(stmt, "long long %lld convertible to w-string `" LWPD "` on "
			"%zd octets.", ll, xsrc.w.str, xsrc.w.cnt);
	} else {
		DBGH(stmt, "long long %lld convertible to string `" LCPD "` on "
			"%zd octets.", ll, xsrc.c.str, xsrc.c.cnt);
	}
	ret = transfer_xstr0(arec, irec, &xsrc, data_ptr, octet_len_ptr);

	/* need to change the error code from truncation to "out of
	 * range", since "whole digits" are truncated */
	if (ret == SQL_SUCCESS_WITH_INFO &&
		HDRH(stmt)->diag.state == SQL_STATE_01004) {
		if (STMT_GD_CALLING(stmt)) {
			return ret;
		} else {
			REJECT_AS_OOR(stmt, ll, /*fixed?*/TRUE, "[STRING]<[value]");
		}
	}
	return SQL_SUCCESS;
}

SQLRETURN sql2c_longlong(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, long long ll)
{
	esodbc_stmt_st *stmt;
	void *data_ptr;
	SQLLEN *octet_len_ptr;
	esodbc_desc_st *ard, *ird;
	SQLSMALLINT ctype;
	SQLRETURN ret;

	stmt = arec->desc->hdr.stmt;
	ird = stmt->ird;
	ard = stmt->ard;

	/* pointer where to write how many characters we will/would use */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	/* Assume a C type behind an SQL C type, but check size representation.
	 * Note: won't work if _min==0 is a legit limit */
#	define REJECT_IF_OOR(_stmt, _ll, _min, _max, _sqlctype, _ctype) \
	do { \
		assert(sizeof(_sqlctype) == sizeof(_ctype)); \
		if ((_min && _ll < _min) || _max < _ll) { \
			REJECT_AS_OOR(_stmt, _ll, /*fixed int*/TRUE, _ctype); \
		} \
	} while (0)
	/* Transfer a long long to an SQL integer type.
	 * Uses local vars: stmt, data_ptr, irec, octet_len_ptr. */
#	define TRANSFER_LL(_ll, _min, _max, _sqlctype, _ctype) \
	do { \
		REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr); \
		REJECT_IF_OOR(stmt, _ll, _min, _max, _sqlctype, _ctype); \
		*(_sqlctype *)data_ptr = (_sqlctype)_ll; \
		write_out_octets(octet_len_ptr, sizeof(_sqlctype), irec); \
		DBGH(stmt, "converted long long %lld to " STR(_sqlctype) " 0x%llx.", \
			_ll, (intptr_t)*(_sqlctype *)data_ptr); \
	} while (0)

	switch ((ctype = get_rec_c_type(arec, irec))) {
		case SQL_C_CHAR:
		case SQL_C_WCHAR:
			return longlong_to_str(arec, irec, ll, data_ptr, octet_len_ptr,
					ctype == SQL_C_WCHAR);

		case SQL_C_TINYINT:
		case SQL_C_STINYINT:
			TRANSFER_LL(ll, CHAR_MIN, CHAR_MAX, SQLSCHAR, char);
			break;
		case SQL_C_UTINYINT:
			TRANSFER_LL(ll, 0, UCHAR_MAX, SQLCHAR, unsigned char);
			break;
		case SQL_C_SHORT:
		case SQL_C_SSHORT:
			TRANSFER_LL(ll, SHRT_MIN, SHRT_MAX, SQLSMALLINT, short);
			break;
		case SQL_C_USHORT:
			TRANSFER_LL(ll, 0, USHRT_MAX, SQLUSMALLINT, unsigned short);
			break;
		case SQL_C_LONG:
		case SQL_C_SLONG:
			TRANSFER_LL(ll, LONG_MIN, LONG_MAX, SQLINTEGER, long);
			break;
		case SQL_C_ULONG:
			TRANSFER_LL(ll, 0, ULONG_MAX, SQLUINTEGER, unsigned long);
			break;
		case SQL_C_SBIGINT:
			TRANSFER_LL(ll, LLONG_MIN, LLONG_MAX, SQLBIGINT, long long);
			break;
		case SQL_C_UBIGINT:
			TRANSFER_LL(ll, 0, ULLONG_MAX, SQLUBIGINT, unsigned long long);
			break;

		case SQL_C_BIT:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			if (ll < 0 || 2 <= ll) {
				REJECT_AS_OOR(stmt, ll, /*fixed int*/TRUE, SQL_C_BIT);
			} else { /* 0 or 1 */
				*(SQLCHAR *)data_ptr = (SQLCHAR)ll;
			}
			write_out_octets(octet_len_ptr, sizeof(SQLSCHAR), irec);
			break;

		case SQL_C_NUMERIC:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			ret = double_to_numeric(arec, (double)ll, data_ptr);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			write_out_octets(octet_len_ptr, sizeof(SQL_NUMERIC_STRUCT), irec);
			break;

		case SQL_C_FLOAT:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			REJECT_IF_OOR(stmt, ll, -FLT_MAX, FLT_MAX, SQLREAL, float);
			*(SQLREAL *)data_ptr = (SQLREAL)ll;
			write_out_octets(octet_len_ptr, sizeof(SQLREAL), irec);
			break;

		case SQL_C_DOUBLE:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			REJECT_IF_OOR(stmt, ll, -DBL_MAX, DBL_MAX, SQLDOUBLE, double);
			*(SQLDOUBLE *)data_ptr = (SQLDOUBLE)ll;
			write_out_octets(octet_len_ptr, sizeof(SQLDOUBLE), irec);
			break;

		case SQL_C_BINARY:
			return llong_to_binary(arec, irec, ll, data_ptr, octet_len_ptr);

		default:
			BUGH(stmt, "unexpected unhanlded data type: %d.",
				get_rec_c_type(arec, irec));
			return SQL_ERROR;
	}
	DBGH(stmt, "REC@0x%p, data_ptr@0x%p, copied long long: %lld.", arec,
		data_ptr, ll);

	return SQL_SUCCESS;

#	undef REJECT_IF_OOR
#	undef TRANSFER_LL
}

static SQLRETURN double_to_bit(esodbc_rec_st *arec, esodbc_rec_st *irec,
	double src, void *data_ptr, SQLLEN *octet_len_ptr)
{
	esodbc_state_et state = SQL_STATE_00000;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);

	write_out_octets(octet_len_ptr, sizeof(SQLCHAR), irec);

	if (src < 0. || 2. <= src) {
		REJECT_AS_OOR(stmt, src, /*fixed?*/FALSE, SQL_C_BIT);
	} else if (0. < src && src < 1.) {
		*(SQLCHAR *)data_ptr = 0;
		state = SQL_STATE_01S07;
	} else if (1. < src && src < 2.) {
		*(SQLCHAR *)data_ptr = 1;
		state = SQL_STATE_01S07;
	} else { /* 0 or 1 */
		*(SQLCHAR *)data_ptr = (SQLCHAR)src;
	}
	if (state != SQL_STATE_00000) {
		INFOH(stmt, "truncating when converting %f as %d.", src,
			*(SQLCHAR *)data_ptr);
		RET_HDIAGS(stmt, state);
	}

	DBGH(stmt, "double %f converted to bit %d.", src, *(SQLCHAR *)data_ptr);

	return SQL_SUCCESS;
}

static SQLRETURN double_to_binary(esodbc_rec_st *arec, esodbc_rec_st *irec,
	double dbl, void *data_ptr, SQLLEN *octet_len_ptr)
{
	size_t cnt;
	double udbl = dbl < 0. ? -dbl : dbl;
	float flt;
	char *ptr;
	esodbc_state_et state = SQL_STATE_00000;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	if (udbl < FLT_MIN || FLT_MAX < udbl) {
		/* value's precision/scale requires a double */
		cnt = sizeof(dbl);
		ptr = (char *)&dbl;
	} else {
		flt = (float)dbl;
		cnt = sizeof(flt);
		ptr = (char *)&flt;
	}

	cnt = buff_octet_size(cnt, sizeof(*ptr), arec, irec, &state);
	if (state) {
		REJECT_AS_OOR(stmt, dbl, /*fixed?*/FALSE, "[BINARY]<[floating]");
	}
	write_out_octets(octet_len_ptr, cnt, irec);
	if (data_ptr) {
		memcpy(data_ptr, ptr, cnt);
		//TODO: should the driver clear all the received buffer?? Cfg option?
		//memset((char *)data_ptr + cnt, 0, arec->octet_length - cnt);
	}

	DBGH(stmt, "converted double %f to binary on %zd octets.", dbl, cnt);

	return SQL_SUCCESS;
}

/*
 * TODO!!!
 * 1. use default precision
 * 2. config for scientific notation.
 * 3. sprintf (for now)
 */
static SQLRETURN double_to_str(esodbc_rec_st *arec, esodbc_rec_st *irec,
	double dbl, void *data_ptr, SQLLEN *octet_len_ptr, BOOL wide)
{
	long long whole;
	unsigned long long fraction;
	double rest;
	SQLSMALLINT scale;
	size_t pos, octets;
	/* buffer unit size */
	size_t usize = wide ? sizeof(SQLWCHAR) : sizeof(SQLCHAR);
	esodbc_state_et state = SQL_STATE_00000;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	/* buffer is overprovisioned for !wide, but avoids double declaration */
	SQLCHAR buff[(2 * ESODBC_PRECISION_INT64 + /*.*/1 + /*\0*/1)
		* sizeof(SQLWCHAR)];
	xstr_st xstr = (xstr_st) {
		.wide = wide,
		.c  = (cstr_st) {
			/* same vals for both wide and C strings */
			.str = buff,
			.cnt = 0
		}
	};

	/*
	 * split the whole and fractional parts
	 */
	assert(sizeof(dbl) == sizeof(whole)); /* [double]==[long long] */
	whole = (long long)dbl;
	rest = dbl - whole;

	/* retain user defined or data source default number of fraction digits */
	scale = 0 < arec->scale ? arec->scale : irec->es_type->maximum_scale;
	rest *= pow10(scale);
	rest = round(rest);
	fraction = rest < 0 ? (unsigned long long) -rest
		: (unsigned long long)rest;

	/* copy integer part into work buffer */
	pos = i64tot((int64_t)whole, buff, wide);
	/* would writing just the whole part + \0 fit into the buffer? */
	octets = buff_octet_size((pos + 1) * usize, usize, arec, irec, &state);
	if (state) {
		REJECT_AS_OOR(stmt, dbl, /*fixed?*/FALSE, "[STRING]<[floating.whole]");
	} else {
		assert(octets == (pos + 1) * usize);
	}

	if (wide) {
		((SQLWCHAR *)buff)[pos ++] = L'.';
	} else {
		((SQLCHAR *)buff)[pos ++] = '.';
	}

	/* copy fractional part into work buffer */
	pos += ui64tot((uint64_t)fraction, (char *)buff + pos * usize, wide);

	xstr.w.cnt = pos; /*==.c.cnt*/

	return transfer_xstr0(arec, irec, &xstr, data_ptr, octet_len_ptr);
}

SQLRETURN sql2c_double(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, double dbl)
{
	esodbc_stmt_st *stmt;
	void *data_ptr;
	SQLLEN *octet_len_ptr;
	esodbc_desc_st *ard, *ird;
	SQLSMALLINT ctype;
	SQLRETURN ret;
	double udbl;

	stmt = arec->desc->hdr.stmt;
	ird = stmt->ird;
	ard = stmt->ard;

	/* pointer where to write how many characters we will/would use */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	/* Transfer a double to an SQL integer type.
	 * Uses local vars: stmt, data_ptr, irec, octet_len_ptr.
	 * Returns - RET_ - 01S07 on success (due to truncation of fractionals). */
#	define RET_TRANSFER_DBL(_dbl, _min, _max, _sqlctype, _ctype) \
	do { \
		/* using C type limits, so check C and SQL C type precision */ \
		assert(sizeof(_sqlctype) == sizeof(_ctype)); \
		if (_dbl) { \
			if ((_sqlctype)_dbl < _min || _max < (_sqlctype)_dbl) { \
				REJECT_AS_OOR(stmt, _dbl, /*fixed?*/FALSE, _sqlctype); \
			} \
		} else { \
			double __udbl = dbl < 0 ? -dbl : dbl; \
			if (_max < (_sqlctype)__udbl) { \
				REJECT_AS_OOR(stmt, _dbl, /*fixed?*/FALSE, _sqlctype); \
			} \
		} \
		*(_sqlctype *)data_ptr = (_sqlctype)_dbl; \
		write_out_octets(octet_len_ptr, sizeof(_sqlctype), irec); \
		DBGH(stmt, "converted double %f to " STR(_sqlctype) " 0x%llx.", _dbl, \
			(intptr_t)*(_sqlctype *)data_ptr); \
		RET_HDIAGS(stmt, SQL_STATE_01S07); \
	} while (0)

	switch ((ctype = get_rec_c_type(arec, irec))) {
		case SQL_C_CHAR:
		case SQL_C_WCHAR:
			return double_to_str(arec, irec, dbl, data_ptr, octet_len_ptr,
					ctype == SQL_C_WCHAR);

		case SQL_C_TINYINT:
		case SQL_C_STINYINT:
			RET_TRANSFER_DBL(dbl, CHAR_MIN, CHAR_MAX, SQLSCHAR, char);
		case SQL_C_UTINYINT:
			RET_TRANSFER_DBL(dbl, 0, UCHAR_MAX, SQLCHAR, unsigned char);
		case SQL_C_SBIGINT:
			RET_TRANSFER_DBL(dbl, LLONG_MIN, LLONG_MAX, SQLBIGINT, long long);
		case SQL_C_UBIGINT:
			RET_TRANSFER_DBL(dbl, 0, LLONG_MAX, SQLUBIGINT, long long);
		case SQL_C_SHORT:
		case SQL_C_SSHORT:
			RET_TRANSFER_DBL(dbl, SHRT_MIN, SHRT_MAX, SQLSMALLINT, short);
		case SQL_C_USHORT:
			RET_TRANSFER_DBL(dbl, 0, USHRT_MAX, SQLUSMALLINT, unsigned short);
		case SQL_C_LONG:
		case SQL_C_SLONG:
			RET_TRANSFER_DBL(dbl, LONG_MIN, LONG_MAX, SQLINTEGER, long);
		case SQL_C_ULONG:
			RET_TRANSFER_DBL(dbl, 0, ULONG_MAX, SQLINTEGER, unsigned long);

		case SQL_C_NUMERIC:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			ret = double_to_numeric(arec, dbl, data_ptr);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			write_out_octets(octet_len_ptr, sizeof(SQL_NUMERIC_STRUCT), irec);
			break;

		case SQL_C_FLOAT:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			udbl = dbl < 0 ? -dbl : dbl;
			if (udbl < FLT_MIN || FLT_MAX < udbl) {
				REJECT_AS_OOR(stmt, dbl, /* is fixed */FALSE, SQLREAL);
			}
			*(SQLREAL *)data_ptr = (SQLREAL)dbl;
			write_out_octets(octet_len_ptr, sizeof(SQLREAL), irec);
			break;
		case SQL_C_DOUBLE:
			REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
			*(SQLDOUBLE *)data_ptr = dbl;
			write_out_octets(octet_len_ptr, sizeof(SQLDOUBLE), irec);
			break;

		case SQL_C_BIT:
			return double_to_bit(arec, irec, dbl, data_ptr, octet_len_ptr);

		case SQL_C_BINARY:
			return double_to_binary(arec, irec, dbl, data_ptr, octet_len_ptr);

		default:
			BUGH(stmt, "unexpected unhanlded data type: %d.",
				get_rec_c_type(arec, irec));
			return SQL_ERROR;
	}

	DBGH(stmt, "REC@0x%p, data_ptr@0x%p, copied double: %.6e.", arec,
		data_ptr, dbl);

	return SQL_SUCCESS;

#	undef RET_TRANSFER_DBL
}

static SQLRETURN wstr_to_cstr(esodbc_rec_st *arec, esodbc_rec_st *irec,
	void *data_ptr, SQLLEN *octet_len_ptr,
	const wchar_t *wstr, size_t chars_0)
{
	esodbc_state_et state = SQL_STATE_00000;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	char *charp;
	int in_bytes, out_bytes, c;
	xstr_st xstr = (xstr_st) {
		.wide = TRUE,
		.w = (wstr_st) {
			(SQLWCHAR *)wstr, chars_0 - 1
		}
	};

	gd_offset_apply(stmt, &xstr);

	if (data_ptr) {
		charp = (char *)data_ptr;

		in_bytes = (int)buff_octet_size((xstr.w.cnt + 1) * sizeof(SQLWCHAR),
				sizeof(SQLCHAR), arec, irec, &state);
		/* trim the original string until it fits in output buffer, with given
		 * length limitation */
		for (c = (int)xstr.w.cnt + 1; 0 < c; c --) {
			out_bytes = WCS2U8(xstr.w.str, c, charp, in_bytes);
			if (out_bytes <= 0) {
				if (WCS2U8_BUFF_INSUFFICIENT) {
					continue;
				}
				ERRNH(stmt, "failed to convert wchar_t* to char* for string `"
					LWPDL "`.", c, xstr.w.str);
				RET_HDIAGS(stmt, SQL_STATE_22018);
			} else {
				/* conversion succeeded */
				break;
			}
		}

		/* if 0's present => 0 < out_bytes */
		assert(xstr.w.str[xstr.w.cnt] == L'\0');
		assert(0 < out_bytes);
		/* if user gives 0 as buffer size, out_bytes will also be 0 */
		if (charp[out_bytes - 1]) {
			/* ran out of buffer => not 0-terminated and truncated already */
			charp[out_bytes - 1] = 0;
			state = SQL_STATE_01004; /* indicate truncation */
			c --; /* last char was overwritten with 0 -> dec xfed count */
		}

		/* only update offset if data is copied out */
		gd_offset_update(stmt, xstr.w.cnt, c);

		DBGH(stmt, "REC@0x%p, data_ptr@0x%p, copied %zd bytes: `" LWPD "`.",
			arec, data_ptr, out_bytes, charp);
	} else {
		DBGH(stmt, "REC@0x%p, NULL data_ptr.", arec);
	}

	/* if length needs to be given, calculate it (not truncated) & converted */
	if (octet_len_ptr) {
		out_bytes = (size_t)WCS2U8(xstr.w.str, (int)xstr.w.cnt + 1, NULL, 0);
		if (out_bytes <= 0) {
			ERRNH(stmt, "failed to convert wchar* to char* for string `"
				LWPDL "`.", LWSTR(&xstr.w));
			RET_HDIAGS(stmt, SQL_STATE_22018);
		} else {
			/* chars_0 accounts for 0-terminator, so WCS2U8 will count that in
			 * the output as well => trim it, since we must not count it when
			 * indicating the length to the application */
			out_bytes --;
		}
		write_out_octets(octet_len_ptr, out_bytes, irec);
	} else {
		DBGH(stmt, "REC@0x%p, NULL octet_len_ptr.", arec);
	}

	if (state != SQL_STATE_00000) {
		RET_HDIAGS(stmt, state);
	}
	return SQL_SUCCESS;
}

/*
 * -> SQL_C_WCHAR
 * Note: chars_0 accounts for 0-term, but length indicated back to the
 * application must not.
 */
static SQLRETURN wstr_to_wstr(esodbc_rec_st *arec, esodbc_rec_st *irec,
	void *data_ptr, SQLLEN *octet_len_ptr,
	const wchar_t *wstr, size_t chars_0)
{
	xstr_st xsrc = (xstr_st) {
		.wide = TRUE,
		.w = (wstr_st) {
			(SQLWCHAR *)wstr, chars_0 - 1
		}
	};
	return transfer_xstr0(arec, irec, &xsrc, data_ptr, octet_len_ptr);
}

/* Converts an xstr to a TS.
 * xstr needs to be trimmed to exact data (no padding, no 0-term counted).
 * If ts_buff is non-NULL, the xstr will be copied (possibly W-to-C converted)
 * into it. */
static BOOL xstr_to_timestamp_struct(xstr_st *xstr, TIMESTAMP_STRUCT *tss,
	cstr_st *ts_buff)
{
	/* need the 0-term in the buff, since ansi_w2c will write it */
	char buff[sizeof(ESODBC_ISO8601_TEMPLATE)/*+\0*/];
	cstr_st ts_str, *ts_ptr;
	timestamp_t tsp;
	struct tm tmp;

	if (ts_buff) {
		assert(sizeof(ESODBC_ISO8601_TEMPLATE) - 1 <= ts_buff->cnt);
		ts_ptr = ts_buff;
	} else {
		ts_str.str = buff;
		ts_str.cnt = sizeof(buff);
		ts_ptr = &ts_str;
	}

	if (xstr->wide) {
		DBG("converting ISO 8601 `" LWPDL "` to timestamp.", LWSTR(&xstr->w));
		if (sizeof(ESODBC_ISO8601_TEMPLATE) - 1 < xstr->w.cnt) {
			ERR("`" LWPDL "` not a TIMESTAMP.", LWSTR(&xstr->w));
			return FALSE;
		}
		/* convert the W-string to C-string; also, copy it directly into out
		 * ts_buff, if given (thus saving one extra copying) */
		ts_ptr->cnt = ansi_w2c(xstr->w.str, ts_ptr->str, xstr->w.cnt) - 1;
	} else {
		DBG("converting ISO 8601 `" LCPDL "` to timestamp.", LCSTR(&xstr->c));
		if (sizeof(ESODBC_ISO8601_TEMPLATE) - 1 < xstr->c.cnt) {
			ERR("`" LCPDL "` not a TIMESTAMP.", LCSTR(&xstr->c));
			return FALSE;
		}
		/* no conversion needed; but copying to the out ts_buff, if given */
		if (ts_buff) {
			memcpy(ts_ptr->str, xstr->c.str, xstr->c.cnt);
			ts_ptr->cnt = xstr->c.cnt;
		} else {
			ts_ptr = &xstr->c;
		}
	}

	/* len counts the 0-term */
	if (ts_ptr->cnt <= 1 || timestamp_parse(ts_ptr->str, ts_ptr->cnt, &tsp) ||
		(! timestamp_to_tm_local(&tsp, &tmp))) {
		ERR("data `" LCPDL "` not an ANSI ISO 8601 format.", LCSTR(ts_ptr));
		return FALSE;
	}
	TM_TO_TIMESTAMP_STRUCT(&tmp, tss);
	tss->fraction = tsp.nsec / 1000000;

	DBG("parsed ISO 8601: `%04d-%02d-%02dT%02d:%02d:%02d.%u+%dm`.",
		tss->year, tss->month, tss->day,
		tss->hour, tss->minute, tss->second, tss->fraction,
		tsp.offset);

	return TRUE;
}


static BOOL parse_timedate(xstr_st *xstr, TIMESTAMP_STRUCT *tss,
	SQLSMALLINT *format, cstr_st *ts_buff)
{
	/* template buffer: date or time values will be copied in place and
	 * evaluated as a timestamp (needs to be valid) */
	SQLCHAR templ[] = "0001-01-01T00:00:00.0000000Z";
	/* conversion Wide to C-string buffer */
	SQLCHAR w2c[sizeof(ESODBC_ISO8601_TEMPLATE)/*+\0*/];
	cstr_st td;/* timedate string */
	xstr_st xtd;

	/* is this a TIMESTAMP? */
	if (sizeof(ESODBC_TIME_TEMPLATE) - 1 < XSTR_LEN(xstr)) {
		/* longer than a date-value -> try a timestamp */
		if (! xstr_to_timestamp_struct(xstr, tss, ts_buff)) {
			return FALSE;
		}
		if (format) {
			*format = SQL_TYPE_TIMESTAMP;
		}
		return TRUE;
	}

	/* W-strings will eventually require convertion to C-string for TS
	 * conversion => do it now to simplify string analysis */
	if (xstr->wide) {
		td.cnt = ansi_w2c(xstr->w.str, w2c, xstr->w.cnt) - 1;
		td.str = w2c;
	} else {
		td = xstr->c;
	}
	xtd.wide = FALSE;

	/* could this be a TIME-val? */
	if (/*hh:mm:ss*/8 <= td.cnt && td.str[2] == ':' && td.str[5] == ':') {
		/* copy active value in template and parse it as TS */
		/* copy is safe: cnt <= [time template] < [templ] */
		memcpy(templ + sizeof(ESODBC_DATE_TEMPLATE) - 1, td.str, td.cnt);
		/* there could be a varying number of fractional digits */
		templ[sizeof(ESODBC_DATE_TEMPLATE) - 1 + td.cnt] = 'Z';
		xtd.c.str = templ;
		xtd.c.cnt = td.cnt + sizeof(ESODBC_DATE_TEMPLATE);
		if (! xstr_to_timestamp_struct(&xtd, tss, ts_buff)) {
			ERR("`" LCPDL "` not a TIME.", LCSTR(&td));
			return FALSE;
		} else {
			tss->year = tss->month = tss->day = 0;
			if (format) {
				*format = SQL_TYPE_TIME;
			}
		}
		return TRUE;
	}

	/* could this be a DATE-val? */
	if (/*yyyy-mm-dd*/10 <= td.cnt && td.str[4] == '-' && td.str[7] == '-') {
		/* copy active value in template and parse it as TS */
		/* copy is safe: cnt <= [time template] < [templ] */
		memcpy(templ, td.str, td.cnt);
		xtd.c.str = templ;
		xtd.c.cnt = sizeof(templ)/sizeof(templ[0]) - 1;
		if (! xstr_to_timestamp_struct(&xtd, tss, ts_buff)) {
			ERR("`" LCPDL "` not a DATE.", LCSTR(&td));
			return FALSE;
		} else {
			tss->hour = tss->minute = tss->second = 0;
			tss->fraction = 0;
			if (format) {
				*format = SQL_TYPE_DATE;
			}
		}
		return TRUE;
	}

	ERR("`" LCPDL "` not a Time/Date/Timestamp.", LCSTR(&td));
	return FALSE;
}

/*
 * -> SQL_C_TYPE_TIMESTAMP
 *
 * Conversts an ES/SQL 'date' or a text representation of a
 * timestamp/date/time value into a TIMESTAMP_STRUCT (indicates the detected
 * input format into the "format" parameter).
 */
static SQLRETURN wstr_to_timestamp(esodbc_rec_st *arec, esodbc_rec_st *irec,
	void *data_ptr, SQLLEN *octet_len_ptr,
	const wchar_t *w_str, size_t chars_0, SQLSMALLINT *format)
{
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	TIMESTAMP_STRUCT *tss = (TIMESTAMP_STRUCT *)data_ptr;
	xstr_st xstr = (xstr_st) {
		.wide = TRUE,
		.w = (wstr_st) {
			(SQLWCHAR *)w_str, chars_0 - 1
		}
	};

	if (octet_len_ptr) {
		*octet_len_ptr = sizeof(*tss);
	}

	if (data_ptr) {
		/* right & left trim the data before attempting conversion */
		wtrim_ws(&xstr.w);

		switch (irec->concise_type) {
			case SQL_TYPE_TIMESTAMP:
				if (! xstr_to_timestamp_struct(&xstr, tss, NULL)) {
					RET_HDIAGS(stmt, SQL_STATE_22018);
				}
				if (format) {
					*format = SQL_TYPE_TIMESTAMP;
				}
				break;
			case SQL_VARCHAR:
				if (! parse_timedate(&xstr, tss, format, NULL)) {
					RET_HDIAGS(stmt, SQL_STATE_22018);
				}
				break;

			case SQL_CHAR:
			case SQL_LONGVARCHAR:
			case SQL_WCHAR:
			case SQL_WLONGVARCHAR:
			case SQL_TYPE_DATE:
			case SQL_TYPE_TIME:
				BUGH(stmt, "unexpected (but permitted) SQL type.");
				RET_HDIAGS(stmt, SQL_STATE_HY004);
			default:
				BUGH(stmt, "uncought invalid conversion.");
				RET_HDIAGS(stmt, SQL_STATE_07006);
		}
	} else {
		DBGH(stmt, "REC@0x%p, NULL data_ptr", arec);
	}

	return SQL_SUCCESS;
}

/*
 * -> SQL_C_TYPE_DATE
 */
static SQLRETURN wstr_to_date(esodbc_rec_st *arec, esodbc_rec_st *irec,
	void *data_ptr, SQLLEN *octet_len_ptr,
	const wchar_t *wstr, size_t chars_0)
{
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	DATE_STRUCT *ds = (DATE_STRUCT *)data_ptr;
	TIMESTAMP_STRUCT tss;
	SQLRETURN ret;
	SQLSMALLINT fmt;

	if (octet_len_ptr) {
		*octet_len_ptr = sizeof(*ds);
	}

	if (data_ptr) {
		ret = wstr_to_timestamp(arec, irec, &tss, NULL, wstr, chars_0, &fmt);
		if (! SQL_SUCCEEDED(ret)) {
			return ret;
		}
		if (fmt == SQL_TYPE_TIME) {
			/* it's a time-value */
			RET_HDIAGS(stmt, SQL_STATE_22018);
		}
		ds->year = tss.year;
		ds->month = tss.month;
		ds->day = tss.day;
		if (tss.hour || tss.minute || tss.second || tss.fraction) {
			/* value's truncated */
			RET_HDIAGS(stmt, SQL_STATE_01S07);
		}
	} else {
		DBGH(stmt, "REC@0x%p, NULL data_ptr", arec);
	}

	return SQL_SUCCESS;
}

/*
 * -> SQL_C_TYPE_TIME
 */
static SQLRETURN wstr_to_time(esodbc_rec_st *arec, esodbc_rec_st *irec,
	void *data_ptr, SQLLEN *octet_len_ptr,
	const wchar_t *wstr, size_t chars_0)
{
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;
	TIME_STRUCT *ts = (TIME_STRUCT *)data_ptr;
	TIMESTAMP_STRUCT tss;
	SQLRETURN ret;
	SQLSMALLINT fmt;

	if (octet_len_ptr) {
		*octet_len_ptr = sizeof(*ts);
	}

	if (data_ptr) {
		ret = wstr_to_timestamp(arec, irec, &tss, NULL, wstr, chars_0, &fmt);
		if (! SQL_SUCCEEDED(ret)) {
			return ret;
		}
		/* need to differentiate between:
		 * - 1234-12-34T00:00:00Z : valid and
		 * - 1234-12-34 : invalid */
		if (fmt == SQL_TYPE_DATE) {
			RET_HDIAGS(stmt, SQL_STATE_22018);
		}
		ts->hour = tss.hour;
		ts->minute = tss.minute;
		ts->second = tss.second;
		if (tss.fraction) {
			/* value's truncated */
			RET_HDIAGS(stmt, SQL_STATE_01S07);
		}
	} else {
		DBGH(stmt, "REC@0x%p, NULL data_ptr", arec);
	}

	return SQL_SUCCESS;
}

/*
 * wstr: is 0-terminated and terminator is counted in 'chars_0'.
 * However: "[w]hen C strings are used to hold character data, the
 * null-termination character is not considered to be part of the data and is
 * not counted as part of its byte length."
 * "If the data was converted to a variable-length data type, such as
 * character or binary [...][i]t then null-terminates the data."
 */
SQLRETURN sql2c_string(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, const wchar_t *wstr, size_t chars_0)
{
	esodbc_stmt_st *stmt;
	void *data_ptr;
	SQLLEN *octet_len_ptr;
	esodbc_desc_st *ard, *ird;
	SQLSMALLINT ctarget;
	long long ll;
	unsigned long long ull;
	wstr_st wval;
	double dbl;
	SQLWCHAR *endp;

	stmt = arec->desc->hdr.stmt;
	ird = stmt->ird;
	ard = stmt->ard;

	/* pointer where to write how many characters we will/would use */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	switch ((ctarget = get_rec_c_type(arec, irec))) {
		case SQL_C_CHAR:
			return wstr_to_cstr(arec, irec, data_ptr, octet_len_ptr,
					wstr, chars_0);
		case SQL_C_BINARY: /* treat binary as WCHAR */ // TODO: add \0???
		case SQL_C_WCHAR:
			return wstr_to_wstr(arec, irec, data_ptr, octet_len_ptr,
					wstr, chars_0);

		case SQL_C_TYPE_TIMESTAMP:
			return wstr_to_timestamp(arec, irec, data_ptr, octet_len_ptr,
					wstr, chars_0, NULL);
		case SQL_C_TYPE_DATE:
			return wstr_to_date(arec, irec, data_ptr, octet_len_ptr,
					wstr, chars_0);
		case SQL_C_TYPE_TIME:
			return wstr_to_time(arec, irec, data_ptr, octet_len_ptr,
					wstr, chars_0);

		case SQL_C_TINYINT:
		case SQL_C_STINYINT:
		case SQL_C_SHORT:
		case SQL_C_SSHORT:
		case SQL_C_LONG:
		case SQL_C_SLONG:
		case SQL_C_SBIGINT:
			wval = (wstr_st) {
				(SQLWCHAR *)wstr, chars_0 - 1
			};
			/* trim any white spaces */
			wtrim_ws(&wval);
			/* convert to integer type */
			errno = 0;
			if (! str2bigint(&wval, /*wide?*/TRUE, (SQLBIGINT *)&ll)) {
				ERRH(stmt, "can't convert `" LWPD "` to long long.", wstr);
				RET_HDIAGS(stmt, errno == ERANGE ? SQL_STATE_22003 :
					SQL_STATE_22018);
			}
			DBGH(stmt, "string `" LWPD "` converted to LL=%lld.", wstr, ll);
			/* delegate to existing functionality */
			return sql2c_longlong(arec, irec, pos, ll);

		case SQL_C_UTINYINT:
		case SQL_C_USHORT:
		case SQL_C_ULONG:
		case SQL_C_UBIGINT:
			wval = (wstr_st) {
				(SQLWCHAR *)wstr, chars_0 - 1
			};
			/* trim any white spaces */
			wtrim_ws(&wval);
			/* convert to integer type */
			errno = 0;
			if (! str2ubigint(&wval, /*wide?*/TRUE, (SQLUBIGINT *)&ull)) {
				ERRH(stmt, "can't convert `" LWPD "` to unsigned long long.",
					wstr);
				RET_HDIAGS(stmt, errno == ERANGE ? SQL_STATE_22003 :
					SQL_STATE_22018);
			}
			DBGH(stmt, "string `" LWPD "` converted to ULL=%llu.", wstr, ull);
			if (ull <= LLONG_MAX) {
				/* the cast is safe, delegate to existing functionality */
				return sql2c_longlong(arec, irec, pos, (long long)ull);
			}
			/* value is larger than what long long can hold: can only convert
			 * to SQLUBIGINT (and SQLULONG, if it has the same size), or fail
			 * as out-of-range */
			assert(sizeof(SQLUBIGINT) == sizeof(unsigned long long));
			if ((ctarget == SQL_C_UBIGINT) || (ctarget == SQL_C_ULONG &&
					sizeof(SQLUINTEGER) == sizeof(SQLUBIGINT))) {
				/* write out the converted value */
				REJECT_IF_NULL_DEST_BUFF(stmt, data_ptr);
				*(SQLUBIGINT *)data_ptr = (SQLUBIGINT)ull;
				write_out_octets(octet_len_ptr, sizeof(SQLUBIGINT), irec);
				DBGH(stmt, "converted string `" LWPD "` to "
					"unsigned long long %llu.", wstr, ull);
			} else {
				REJECT_AS_OOR(stmt, ull, /*fixed?*/TRUE, "non-ULL");
			}
			break;

		case SQL_C_FLOAT:
		case SQL_C_DOUBLE:
		case SQL_C_NUMERIC:
		case SQL_C_BIT:
			wval = (wstr_st) {
				(SQLWCHAR *)wstr, chars_0 - 1
			};
			/* trim any white spaces */
			wtrim_ws(&wval);
			/* convert to double */
			errno = 0;
			dbl = wcstod((wchar_t *)wval.str, (wchar_t **)&endp);
			DBGH(stmt, "string `" LWPD "` converted to dbl=%.6e.", wstr, dbl);
			/* if empty string, non-numeric or under/over-flow, bail out */
			if ((! wval.cnt) || (wval.str + wval.cnt != endp) || errno) {
				ERRH(stmt, "can't convert `" LWPD "` to double.", wstr);
				RET_HDIAGS(stmt, errno == ERANGE ? SQL_STATE_22003 :
					SQL_STATE_22018);
			}
			/* delegate to existing functionality */
			return sql2c_double(arec, irec, pos, dbl);


		default:
			BUGH(stmt, "unexpected unhandled data type: %d.",
				get_rec_c_type(arec, irec));
			return SQL_ERROR;
	}

	return SQL_SUCCESS;
}


/* TODO: implementation for the below */
static inline BOOL conv_implemented(SQLSMALLINT sqltype, SQLSMALLINT ctype)
{
	switch (ctype) {
		case SQL_C_GUID:

		case SQL_C_INTERVAL_DAY:
		case SQL_C_INTERVAL_HOUR:
		case SQL_C_INTERVAL_MINUTE:
		case SQL_C_INTERVAL_SECOND:
		case SQL_C_INTERVAL_DAY_TO_HOUR:
		case SQL_C_INTERVAL_DAY_TO_MINUTE:
		case SQL_C_INTERVAL_DAY_TO_SECOND:
		case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		case SQL_C_INTERVAL_HOUR_TO_SECOND:
		case SQL_C_INTERVAL_MINUTE_TO_SECOND:
		case SQL_C_INTERVAL_MONTH:
		case SQL_C_INTERVAL_YEAR:
		case SQL_C_INTERVAL_YEAR_TO_MONTH:
			// case SQL_C_TYPE_TIMESTAMP_WITH_TIMEZONE:
			// case SQL_C_TYPE_TIME_WITH_TIMEZONE:
			return FALSE;
	}

	switch (sqltype) {
		case SQL_C_GUID:

		case SQL_INTERVAL_DAY:
		case SQL_INTERVAL_HOUR:
		case SQL_INTERVAL_MINUTE:
		case SQL_INTERVAL_SECOND:
		case SQL_INTERVAL_DAY_TO_HOUR:
		case SQL_INTERVAL_DAY_TO_MINUTE:
		case SQL_INTERVAL_DAY_TO_SECOND:
		case SQL_INTERVAL_HOUR_TO_MINUTE:
		case SQL_INTERVAL_HOUR_TO_SECOND:
		case SQL_INTERVAL_MINUTE_TO_SECOND:
		case SQL_INTERVAL_MONTH:
		case SQL_INTERVAL_YEAR:
		case SQL_INTERVAL_YEAR_TO_MONTH:
			// case SQL_TYPE_TIMESTAMP_WITH_TIMEZONE:
			// case SQL_TYPE_TIME_WITH_TIMEZONE:
			return FALSE;
	}

	return TRUE;
}


/* check if data types in returned columns are compabile with buffer types
 * bound for those columns
 * idx:
 *     if >= 0: parameter number (0,) for parameter binding;
 *     if < 0: indicator for bound columns check.
 *     */
SQLRETURN convertability_check(esodbc_stmt_st *stmt, SQLINTEGER idx,
	int *conv_code)
{
	SQLINTEGER i, start, stop;
	esodbc_desc_st *axd, *ixd;
	esodbc_rec_st *arec, *irec;

	if (idx < 0) {
		/*
		 * bound columns check
		 */
		assert(stmt->hdr.dbc->es_types);
		assert(STMT_HAS_RESULTSET(stmt));

		axd = stmt->ard;
		ixd = stmt->ird;

		start = 0;
		stop = axd->count < ixd->count ? axd->count : ixd->count;
	} else {
		/*
		 * binding paramter check
		 */
		assert(0 < idx);
		start = idx - 1;
		stop = idx;

		axd = stmt->apd;
		ixd = stmt->ipd;
	}

	for (i = start; i < stop; i ++) {
		assert(i < axd->count);
		arec = &axd->recs[i];
		if ((idx < 0) && (! REC_IS_BOUND(arec))) {
			/* skip not bound columns */
			continue;
		}
		assert(i < ixd->count);
		irec = &ixd->recs[i];

		assert(arec && irec);

		if (! ESODBC_TYPES_COMPATIBLE(irec->concise_type,arec->concise_type)) {
			ERRH(stmt, "conversion not possible on ordinal #%d: IRD: %hd, "
				"ARD: %hd.", i + 1, irec->concise_type, arec->concise_type);
			if (conv_code) {
				*conv_code = CONVERSION_VIOLATION;
			}
			RET_HDIAGS(stmt, SQL_STATE_07006);
		}
		if (! conv_implemented(irec->concise_type, arec->concise_type)) {
			ERRH(stmt, "conversion not supported on ordinal #%d : IRD: %hd, "
				"ARD: %hd.", i + 1, irec->concise_type, arec->concise_type);
			if (conv_code) {
				*conv_code = CONVERSION_UNSUPPORTED;
			}
			RET_HDIAGS(stmt, SQL_STATE_HYC00);
		}
	}

	if (conv_code) {
		*conv_code = CONVERSION_SUPPORTED;
	}
	DBGH(stmt, "convertibility check: OK.");
	return SQL_SUCCESS;
}

#if 0
/* check if data types in returned columns are compabile with buffer types
 * bound for those columns */
SQLRETURN sql2c_convertible(esodbc_stmt_st *stmt)
{
	SQLSMALLINT i, min, ret;
	esodbc_desc_st *ard, *ird;
	esodbc_rec_st *arec, *irec;

	assert(stmt->hdr.dbc->es_types);
	assert(STMT_HAS_RESULTSET(stmt));

	ard = stmt->ard;
	ird = stmt->ird;

	min = ard->count < ird->count ? ard->count : ird->count;
	for (i = 0; i < min; i ++) {
		arec = &ard->recs[i];
		if (! REC_IS_BOUND(arec)) {
			/* skip not bound columns */
			continue;
		}
		irec = &ird->recs[i];

		if (! ESODBC_TYPES_COMPATIBLE(irec->concise_type,arec->concise_type)) {
			ERRH(stmt, "type conversion not possible on column %d: IRD: %hd, "
				"ARD: %hd.", i + 1, irec->concise_type, arec->concise_type);
			stmt->sql2c_conversion = CONVERSION_VIOLATION;
			RET_HDIAGS(stmt, SQL_STATE_07006);
		}
		if (! conv_implemented(irec->concise_type, arec->concise_type)) {
			ERRH(stmt, "conversion not supported on column %d types: IRD: %hd,"
				" ARD: %hd.", i + 1, irec->concise_type, arec->concise_type);
			stmt->sql2c_conversion = CONVERSION_UNSUPPORTED;
			RET_HDIAGS(stmt, SQL_STATE_HYC00);
		}
	}

	stmt->sql2c_conversion = CONVERSION_SUPPORTED;
	DBGH(stmt, "convertibility check: OK.");
	return SQL_SUCCESS;
}
#endif

/* Converts a C/W-string to a u/llong or double (dest_type?); the xstr->wide
 * needs to be set;
 * Returns success of conversion and pointer to trimmed number str
 * representation.  */
static BOOL xstr_to_number(void *data_ptr, SQLLEN *octet_len_ptr,
	xstr_st *xstr, SQLSMALLINT dest_type, void *dest)
{
	BOOL res;

	if (xstr->wide) {
		xstr->w.str = (SQLWCHAR *)data_ptr;
		if ((octet_len_ptr && *octet_len_ptr == SQL_NTSL) || !octet_len_ptr) {
			xstr->w.cnt = wcslen(xstr->w.str);
		} else {
			xstr->w.cnt = (size_t)(*octet_len_ptr / sizeof(*xstr->w.str));
			xstr->w.cnt -= /*0-term*/1;
		}
	} else {
		xstr->c.str = (SQLCHAR *)data_ptr;
		if ((octet_len_ptr && *octet_len_ptr == SQL_NTSL) || !octet_len_ptr) {
			xstr->c.cnt = strlen(xstr->c.str);
		} else {
			xstr->c.cnt = (size_t)(*octet_len_ptr - /*\0*/1);
		}
	}

	if (! dest) {
		return TRUE;
	}

	if (xstr->wide) {
		wtrim_ws(&xstr->w);
		DBG("converting paramter value `" LWPDL "` to number.",
			LWSTR(&xstr->w));
		switch (dest_type) {
			case SQL_C_SBIGINT:
				res = str2bigint(&xstr->w, /*wide?*/TRUE, (SQLBIGINT *)dest);
				break;
			case SQL_C_UBIGINT:
				res = str2bigint(&xstr->w, /*wide?*/TRUE, (SQLUBIGINT *)dest);
				break;
			case SQL_C_DOUBLE:
				res = str2double(&xstr->w, /*wide?*/TRUE, (SQLDOUBLE *)dest);
				break;
			default:
				assert(0);
		}
	} else {
		trim_ws(&xstr->c);
		DBG("converting paramter value `" LCPDL "` to number.",
			LCSTR(&xstr->c));
		switch (dest_type) {
			case SQL_C_SBIGINT:
				res = str2bigint(&xstr->c, /*wide?*/FALSE, (SQLBIGINT *)dest);
				break;
			case SQL_C_UBIGINT:
				res = str2bigint(&xstr->c, /*wide?*/FALSE, (SQLUBIGINT *)dest);
				break;
			case SQL_C_DOUBLE:
				res = str2double(&xstr->c, /*wide?*/FALSE, (SQLDOUBLE *)dest);
				break;
			default:
				assert(0);
		}
	}

	if (! res) {
		if (xstr->wide) {
			ERR("can't convert `" LWPDL "` to type %hd number.",
				LWSTR(&xstr->w), dest_type);
		} else {
			ERR("can't convert `" LCPDL "` to type %hd number.",
				LCSTR(&xstr->c), dest_type);
		}
		return FALSE;
	} else {
		return TRUE;
	}
}


SQLRETURN c2sql_null(esodbc_rec_st *arec,
	esodbc_rec_st *irec, char *dest, size_t *len)
{
	assert(irec->concise_type == ESODBC_SQL_NULL);
	if (dest) {
		memcpy(dest, JSON_VAL_NULL, sizeof(JSON_VAL_NULL) - /*\0*/1);
	}
	*len = sizeof(JSON_VAL_NULL) - /*\0*/1;
	return SQL_SUCCESS;
}

static SQLRETURN double_to_bool(esodbc_stmt_st *stmt, double dbl, BOOL *val)
{
	DBGH(stmt, "converting double %.6e to bool.", dbl);
#ifdef BOOLEAN_IS_BIT
	if (dbl < 0. && 2. < dbl) {
		ERRH(stmt, "double %.6e out of range.", dbl);
		RET_HDIAGS(stmt, SQL_STATE_22003);
	}
	if (0. < dbl && dbl < 2. && dbl != 1.) {
		/* it's a failure, since SUCCESS_WITH_INFO would be returned only
		 * after data is sent to the server.  */
		ERRH(stmt, "double %.6e right truncated.", dbl);
		RET_HDIAGS(stmt, SQL_STATE_22003);
	}
#endif /* BOOLEAN_IS_BIT */
	*val = dbl != 0.;
	return SQL_SUCCESS;
}

SQLRETURN c2sql_boolean(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	BOOL val;
	SQLSMALLINT ctype;
	SQLRETURN ret;
	void *data_ptr;
	SQLLEN *octet_len_ptr;
	xstr_st xstr;
	double dbl;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	if (! dest) {
		/* return the "worst case" len (and only convert data at copy time) */
		*len = sizeof(JSON_VAL_FALSE) - 1;
		return SQL_SUCCESS;
	}

	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	/*INDENT-OFF*/
	switch ((ctype = get_rec_c_type(arec, irec))) {
		case SQL_C_CHAR:
		case SQL_C_WCHAR:
			/* pointer to read from how many bytes we have */
			octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos,
					arec);
			xstr.wide = ctype == SQL_C_WCHAR;
			if (! xstr_to_number(data_ptr, octet_len_ptr, &xstr, SQL_C_DOUBLE,
						&dbl)) {
				RET_HDIAGS(stmt, SQL_STATE_22018);
			}
			ret = double_to_bool(stmt, dbl, &val);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			break;

		do {
		case SQL_C_BINARY:
			/* pointer to read from how many bytes we have */
			octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR,
				pos, arec);
			if (! octet_len_ptr) {
				if (((char *)data_ptr)[0]) {
					RET_HDIAGS(stmt, SQL_STATE_22003);
				}
			} else if (*octet_len_ptr != sizeof(SQLCHAR)) {
				RET_HDIAGS(stmt, SQL_STATE_22003);
			}
		/* no break */
		case SQL_C_BIT:
		case SQL_C_UTINYINT: dbl = (double)*(SQLCHAR *)data_ptr; break;
		case SQL_C_TINYINT:
		case SQL_C_STINYINT: dbl = (double)*(SQLSCHAR *)data_ptr; break;
		case SQL_C_SHORT:
		case SQL_C_SSHORT: dbl = (double)*(SQLSMALLINT *)data_ptr; break;
		case SQL_C_USHORT: dbl = (double)*(SQLUSMALLINT *)data_ptr; break;
		case SQL_C_LONG:
		case SQL_C_SLONG: dbl = (double)*(SQLINTEGER *)data_ptr; break;
		case SQL_C_ULONG: dbl = (double)*(SQLUINTEGER *)data_ptr; break;
		case SQL_C_SBIGINT: dbl = (double)*(SQLBIGINT *)data_ptr; break;
		case SQL_C_UBIGINT: dbl = (double)*(SQLUBIGINT *)data_ptr; break;
		case SQL_C_FLOAT: dbl = (double)*(SQLREAL *)data_ptr; break;
		case SQL_C_DOUBLE: dbl = (double)*(SQLDOUBLE *)data_ptr; break;

		case SQL_C_NUMERIC:
			// TODO: better? *(uul *)val[0] != 0 && *[uul *]val[8] != 0 */
			ret = numeric_to_double(irec, data_ptr, &dbl);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			break;
		} while (0);
			ret = double_to_bool(stmt, dbl, &val);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			break;

		//case SQL_C_BOOKMARK:
		//case SQL_C_VARBOOKMARK:
		default:
			BUGH(stmt, "can't convert SQL C type %hd to boolean.",
				get_rec_c_type(arec, irec));
			RET_HDIAG(stmt, SQL_STATE_HY000, "bug converting parameter", 0);
	}
	/*INDENT-ON*/

	DBGH(stmt, "parameter (pos#%lld) converted to boolean: %d.", pos, val);

	if (val) {
		memcpy(dest, JSON_VAL_TRUE, sizeof(JSON_VAL_TRUE) - /*\0*/1);
		*len = sizeof(JSON_VAL_TRUE) - 1;
	} else {
		memcpy(dest, JSON_VAL_FALSE, sizeof(JSON_VAL_FALSE) - /*\0*/1);
		*len = sizeof(JSON_VAL_FALSE) - 1;
	}
	return SQL_SUCCESS;
}

/*
 * Copies a C/W-string representing a number out to send buffer.
 * wide: type of string;
 * min, max: target SQL numeric type's limits;
 * fixed: target SQL numberic type (integer or floating);
 * dest: buffer's pointer; can be null (when eval'ing) needed space;
 * len: how much of the buffer is needed or has been used.
 */
static SQLRETURN string_to_number(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, BOOL wide, double *min, double *max, BOOL fixed,
	char *dest, size_t *len)
{
	void *data_ptr;
	SQLLEN *octet_len_ptr;
	xstr_st xstr;
	SQLDOUBLE dbl, abs_dbl;
	int ret;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);
	/* pointer to read from how many bytes we have */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);

	xstr.wide = wide;
	/* do a conversion check: use double, as a capture all cases
	 * value: ES/SQL will accept a float for an INTEGER param */
	if (! xstr_to_number(data_ptr, octet_len_ptr, &xstr,
			SQL_C_DOUBLE, dest ? &dbl : NULL)) {
		ERRH(stmt, "failed to convert param value to a double.");
		RET_HDIAGS(stmt, SQL_STATE_22018);
	}

	if (! dest) {
		/* check is superfluous, but safer  */
		*len = wide ? xstr.w.cnt : xstr.c.cnt;
		return SQL_SUCCESS;
	}

	/* check against truncation, limits and any given precision */
	abs_dbl = dbl < 0 ? -dbl : dbl;
	if (fixed) {
		if (0 < abs_dbl - (SQLUBIGINT)abs_dbl) {
			ERRH(stmt, "conversion of double %.6e to fixed would"
				" truncate fractional digits", dbl);
			RET_HDIAGS(stmt, SQL_STATE_22001);
		}
		if ((min && dbl < *min) || (max && *max < dbl)) {
			ERRH(stmt, "converted double %.6e out of bounds "
				"[%.6e, %.6e]", dbl, min, *max);
			/* spec requires 22001 here, but that is wrong? */
			RET_HDIAGS(stmt, SQL_STATE_22003);
		}
	} else {
		if ((min && abs_dbl < *min) || (max && *max < abs_dbl)) {
			ERRH(stmt, "converted abs double %.6e out of bounds "
				"[%.6e, %.6e]", abs_dbl, min, *max);
			RET_HDIAGS(stmt, SQL_STATE_22003);
		}
	}
	//
	// TODO: check IRD precision against avail value?
	//

	/* copy values from app's buffer directly */
	if (wide) { /* need a conversion to ANSI */
		*len = xstr.w.cnt;
		ret = ansi_w2c((SQLWCHAR *)data_ptr, dest, *len);
		assert(0 < ret); /* it converted to a float already */
	} else {
		*len = xstr.c.cnt;
		memcpy(dest, data_ptr, *len);
	}
	return SQL_SUCCESS;
}

static SQLRETURN sfixed_to_number(esodbc_stmt_st *stmt, SQLBIGINT src,
	double *min, double *max, char *dest, size_t *len)
{
	if (! dest) {
		/* largest space it could occupy */
		*len = ESODBC_PRECISION_INT64;
		return SQL_SUCCESS;
	}

	DBGH(stmt, "converting paramter value %lld as number.", src);

	if ((min && src < *min) || (max && *max < src)) {
		ERRH(stmt, "source value %lld out of range [%e, %e] for dest type",
			src, *min, *max);
		RET_HDIAGS(stmt, SQL_STATE_22003);
	}

	assert(sizeof(SQLBIGINT) == sizeof(int64_t));
	*len = i64tot(src, dest, /*wide?*/FALSE);
	assert(*len <= ESODBC_PRECISION_INT64);

	return SQL_SUCCESS;
}

static SQLRETURN ufixed_to_number(esodbc_stmt_st *stmt, SQLUBIGINT src,
	double *max, char *dest, size_t *len)
{
	if (! dest) {
		/* largest space it could occupy */
		*len = ESODBC_PRECISION_UINT64;
		return SQL_SUCCESS;
	}

	DBGH(stmt, "converting paramter value %llu as number.", src);

	if (src < 0 || (max && *max < src)) {
		ERRH(stmt, "source value %llu out of range [0, %e] for dest type",
			src, *max);
		RET_HDIAGS(stmt, SQL_STATE_22003);
	}

	assert(sizeof(SQLBIGINT) == sizeof(int64_t));
	*len = ui64tot(src, dest, /*wide?*/FALSE);
	assert(*len <= ESODBC_PRECISION_UINT64);

	return SQL_SUCCESS;
}

static SQLRETURN floating_to_number(esodbc_rec_st *irec, SQLDOUBLE src,
	double *min, double *max, char *dest, size_t *len)
{
	/* format fixed length in scientific notation, -1.23E-45 */
	//const static size_t ff_len = /*-1.*/3 + /*prec*/0 + /*E-*/ 2 +
	//	sizeof(STR(ESODBC_PRECISION_DOUBLE)) - 1;
	size_t maxlen, width;
	int cnt;
	SQLDOUBLE abs_src;
	esodbc_stmt_st *stmt = irec->desc->hdr.stmt;

	//maxlen = get_param_size(irec) + ff_len;
	maxlen = get_param_size(irec);
	if (! dest) {
		/* largest space it could occupy */
		*len = maxlen + /*0-term, for printf*/1;
		return SQL_SUCCESS;
	}

	abs_src = src < 0 ? -src : src;
	if ((min && abs_src < *min) || (max && *max < abs_src)) {
		ERRH(stmt, "source value %e out of range [%e, %e] for dest type",
			src, *min, *max);
		RET_HDIAGS(stmt, SQL_STATE_22003);
	}

	width = maxlen;
	width -= /*sign*/(src < 0);
	width -= /*1.*/2;
	width -= /*e+00*/4 + /*3rd digit*/(abs_src < 1e-100 || 1e100 < src);
	if (width < 0) {
		ERRH(stmt, "parameter size (%zu) to low for floating point.", maxlen);
		RET_HDIAGS(stmt, SQL_STATE_HY104);
	}
	DBGH(stmt, "converting double param %.6e with precision/width: %zu/%d.",
		src, maxlen, width);
	cnt = snprintf(dest, maxlen + /*\0*/1, "%.*e", (int)width, src);
	if (cnt < 0) {
		ERRH(stmt, "failed to print double %e.", src);
		RET_HDIAGS(stmt, SQL_STATE_HY000);
	} else {
		*len = cnt;
		DBGH(stmt, "value %.6e printed as `" LCPDL "`.", src, *len, dest);
	}

	return SQL_SUCCESS;
}

static SQLRETURN binary_to_number(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	void *data_ptr;
	SQLLEN *octet_len_ptr, osize /*octet~*/;
	SQLBIGINT llng;
	SQLDOUBLE dbl;
	esodbc_stmt_st *stmt = irec->desc->hdr.stmt;

	if (! dest) {
		if (irec->meta_type == METATYPE_EXACT_NUMERIC) {
			return sfixed_to_number(stmt, 0LL, NULL, NULL, NULL, len);
		} else {
			assert(irec->meta_type == METATYPE_FLOAT_NUMERIC);
			return floating_to_number(irec, 0., NULL, NULL, NULL, len);
		}
	}

	/* pointer to read from how many bytes we have */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	if (! octet_len_ptr) {
		/* "If [...] is a null pointer, the driver assumes [...] that
		 * character and binary data is null-terminated." */
		WARNH(stmt, "no length information provided for binary type: "
			"calculating it as a C-string!");
		osize = strlen((char *)data_ptr);
	} else {
		osize = *octet_len_ptr;
	}

#	define CHK_SIZES(_sqlc_type) \
	do { \
		if (osize != sizeof(_sqlc_type)) { \
			ERRH(stmt, "binary data length (%zu) misaligned with target" \
				" data type (%hd) size (%lld)", sizeof(_sqlc_type), \
				irec->es_type->data_type, osize); \
			RET_HDIAGS(stmt, SQL_STATE_HY090); \
		} \
	} while (0)
#	define BIN_TO_LLNG(_sqlc_type) \
	do { \
		CHK_SIZES(_sqlc_type); \
		llng = (SQLBIGINT)*(_sqlc_type *)data_ptr; \
	} while (0)
#	define BIN_TO_DBL(_sqlc_type) \
	do { \
		CHK_SIZES(_sqlc_type); \
		dbl = (SQLDOUBLE)*(_sqlc_type *)data_ptr; \
	} while (0)

	/*INDENT-OFF*/
	switch (irec->es_type->data_type) {
		do {
		/* JSON long */
		case SQL_BIGINT: BIN_TO_LLNG(SQLBIGINT); break; /* LONG */
		case SQL_INTEGER: BIN_TO_LLNG(SQLINTEGER); break; /* INTEGER */
		case SQL_SMALLINT: BIN_TO_LLNG(SQLSMALLINT); break; /* SHORT */
		case SQL_TINYINT: BIN_TO_LLNG(SQLSCHAR); break; /* BYTE */
		} while (0);
			return sfixed_to_number(stmt, llng, NULL, NULL, dest, len);

		/* JSON double */
		do {
			// TODO: check accurate limits for floats in ES/SQL
		case SQL_FLOAT: /* HALF_FLOAT, SCALED_FLOAT */
		case SQL_REAL: BIN_TO_DBL(SQLREAL); break; /* FLOAT */
		case SQL_DOUBLE: BIN_TO_DBL(SQLDOUBLE); break; /* DOUBLE */
		} while (0);
			return floating_to_number(irec, dbl, NULL, NULL, dest, len);
	}
	/*INDENT-ON*/

#	undef BIN_TO_LLNG
#	undef BIN_TO_DBL
#	undef CHK_SIZES

	BUGH(arec->desc->hdr.stmt, "unexpected ES/SQL type %hd.",
		irec->es_type->data_type);
	RET_HDIAG(arec->desc->hdr.stmt, SQL_STATE_HY000,
		"parameter conversion bug", 0);
}

static SQLRETURN numeric_to_number(esodbc_rec_st *irec, void *data_ptr,
	char *dest, size_t *len)
{
	SQLDOUBLE dbl;
	SQLRETURN ret;

	if (! dest) {
		return floating_to_number(irec, 0., NULL, NULL, NULL, len);
	}

	ret = numeric_to_double(irec, data_ptr, &dbl);
	if (! SQL_SUCCEEDED(ret)) {
		return ret;
	}
	return floating_to_number(irec, dbl, NULL, NULL, dest, len);
}

SQLRETURN c2sql_number(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, double *min, double *max, BOOL fixed, char *dest, size_t *len)
{
	void *data_ptr;
	SQLSMALLINT ctype;
	SQLBIGINT llng;
	SQLUBIGINT ullng;
	SQLDOUBLE dbl;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	/*INDENT-OFF*/
	switch ((ctype = get_rec_c_type(arec, irec))) {
		case SQL_C_WCHAR:
		case SQL_C_CHAR:
			return string_to_number(arec, irec, pos, ctype == SQL_C_WCHAR,
					min, max, fixed, dest, len);

		case SQL_C_BINARY:
			return binary_to_number(arec, irec, pos, dest, len);

		do {
		case SQL_C_TINYINT:
		case SQL_C_STINYINT: llng = (SQLBIGINT)*(SQLSCHAR *)data_ptr; break;
		case SQL_C_SHORT:
		case SQL_C_SSHORT: llng = (SQLBIGINT)*(SQLSMALLINT *)data_ptr; break;
		case SQL_C_LONG:
		case SQL_C_SLONG: llng = (SQLBIGINT)*(SQLINTEGER *)data_ptr; break;
		case SQL_C_SBIGINT: llng = *(SQLBIGINT *)data_ptr; break;
		} while (0);
			return sfixed_to_number(stmt, llng, min, max, dest, len);

		do {
		case SQL_C_BIT: // XXX: check if 0/1?
		case SQL_C_UTINYINT: ullng = (SQLUBIGINT)*(SQLCHAR *)data_ptr; break;
		case SQL_C_USHORT: ullng = (SQLUBIGINT)*(SQLUSMALLINT *)data_ptr;break;
		case SQL_C_ULONG: ullng = (SQLUBIGINT)*(SQLUINTEGER *)data_ptr; break;
		case SQL_C_UBIGINT: ullng = *(SQLUBIGINT *)data_ptr; break;
		} while (0);
			return ufixed_to_number(stmt, ullng, max, dest, len);

		do {
		case SQL_C_FLOAT: dbl = (SQLDOUBLE)*(SQLREAL *)data_ptr; break;
		case SQL_C_DOUBLE: dbl = *(SQLDOUBLE *)data_ptr; break;
		} while (0);
			return floating_to_number(irec, dbl, min, max, dest, len);

		case SQL_C_NUMERIC:
			return numeric_to_number(irec, data_ptr, dest, len);

		//case SQL_C_BOOKMARK:
		//case SQL_C_VARBOOKMARK:
		default:
			BUGH(stmt, "can't convert SQL C type %hd to long long.",
				get_rec_c_type(arec, irec));
			RET_HDIAG(stmt, SQL_STATE_HY000, "bug converting parameter", 0);
	}
	/*INDENT-ON*/

	return SQL_SUCCESS;
}

static SQLRETURN convert_str_to_timestamp(esodbc_stmt_st *stmt,
	SQLLEN *octet_len_ptr, void *data_ptr, BOOL wide,
	char *dest, size_t *len)
{
	xstr_st xstr;
	TIMESTAMP_STRUCT tss;
	SQLSMALLINT format;
	cstr_st ts_buff;

	xstr.wide = wide;

	if (wide) {
		xstr.w.str = (SQLWCHAR *)data_ptr;
		if (octet_len_ptr) {
			xstr.w.cnt = *octet_len_ptr / sizeof(SQLWCHAR) - /*\0*/1;
		} else {
			xstr.w.cnt = wcslen(xstr.w.str);
		}
		wtrim_ws(&xstr.w);
	} else {
		xstr.c.str = (SQLCHAR *)data_ptr;
		if (octet_len_ptr) {
			xstr.c.cnt = *octet_len_ptr / sizeof(SQLCHAR) - /*\0*/1;
		} else {
			xstr.c.cnt = strlen(xstr.c.str);
		}
		trim_ws(&xstr.c);
	}

	assert(dest);
	ts_buff.str = dest;
	ts_buff.cnt = sizeof(ESODBC_ISO8601_TEMPLATE) - 1;
	if (! parse_timedate(&xstr, &tss, &format, &ts_buff)) {
		ERRH(stmt, "failed to parse input as Time/Date/Timestamp");
		RET_HDIAGS(stmt, SQL_STATE_22008);
	} else if (format == SQL_TYPE_TIME) {
		ERRH(stmt, "can not convert a Time to a Timestamp value");
		RET_HDIAGS(stmt, SQL_STATE_22018);
	} else {
		/* conversion from TIME to TIMESTAMP should have been deined earlier */
		assert(format != SQL_TYPE_TIME);
		*len += ts_buff.cnt;
	}

	return SQL_SUCCESS;
}

static SQLRETURN convert_ts_to_timestamp(esodbc_stmt_st *stmt,
	SQLLEN *octet_len_ptr, void *data_ptr, SQLSMALLINT ctype,
	char *dest, size_t *len)
{
	TIMESTAMP_STRUCT *tss, buff;
	DATE_STRUCT *ds;
	int cnt;
	size_t osize;

	switch (ctype) {
		case SQL_C_TYPE_DATE:
			ds = (DATE_STRUCT *)data_ptr;
			memset(&buff, 0, sizeof(buff));
			buff.year = ds->year;
			buff.month = ds->month;
			buff.day = ds->day;
			tss = &buff;
			break;
		case SQL_C_BINARY:
			if (! octet_len_ptr) {
				WARNH(stmt, "no length information provided for binary type: "
					"calculating it as a C-string!");
				osize = strlen((char *)data_ptr);
			} else {
				osize = *octet_len_ptr;
			}
			if (osize != sizeof(TIMESTAMP_STRUCT)) {
				ERRH(stmt, "incorrect binary object size: %zu; expected: %zu.",
					osize, sizeof(TIMESTAMP_STRUCT));
				RET_HDIAGS(stmt, SQL_STATE_22003);
			}
		/* no break */
		case SQL_C_TYPE_TIMESTAMP:
			tss = (TIMESTAMP_STRUCT *)data_ptr;
			break;

		default:
			BUGH(stmt, "unexpected SQL C type %hd.", ctype);
			RET_HDIAG(stmt, SQL_STATE_HY000, "param conversion bug", 0);
	}

	assert(dest);
	cnt = snprintf(dest, sizeof(ESODBC_ISO8601_TEMPLATE) - 1,
			"%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
			tss->year, tss->month, tss->day,
			tss->hour, tss->minute, tss->second, tss->fraction);
	if (cnt < 0) {
		ERRH(stmt, "failed printing timestamp struct: %s.", strerror(errno));
		SET_HDIAG(stmt, SQL_STATE_HY000, "C runtime error", 0);
	}
	*len = cnt;

	return SQL_SUCCESS;
}

SQLRETURN c2sql_timestamp(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	void *data_ptr;
	SQLLEN *octet_len_ptr;
	SQLSMALLINT ctype;
	SQLRETURN ret;
	SQLULEN colsize, offt, decdigits;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	if (! dest) {
		/* maximum possible space it can take */
		*len = /*2x `"`*/2 + sizeof(ESODBC_ISO8601_TEMPLATE) - 1;
		return SQL_SUCCESS;
	} else {
		*dest = '"';
	}

	/* pointer to read from how many bytes we have */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	switch ((ctype = get_rec_c_type(arec, irec))) {
		case SQL_C_CHAR:
		case SQL_C_WCHAR:
			ret = convert_str_to_timestamp(stmt, octet_len_ptr, data_ptr,
					ctype == SQL_C_WCHAR, dest + /*`"`*/1, len);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			break;

		case SQL_C_TYPE_TIME:
			// TODO
			ERRH(stmt, "conversion from time to timestamp not implemented.");
			RET_HDIAG(stmt, SQL_STATE_HYC00, "conversion time to timestamp "
				"not yet supported", 0);

		case SQL_C_TYPE_DATE:
		case SQL_C_BINARY:
		case SQL_C_TYPE_TIMESTAMP:
			ret = convert_ts_to_timestamp(stmt, octet_len_ptr, data_ptr,
					ctype, dest + /*`"`*/1, len);
			if (! SQL_SUCCEEDED(ret)) {
				return ret;
			}
			break;

		default:
			BUGH(stmt, "can't convert SQL C type %hd to timestamp.",
				get_rec_c_type(arec, irec));
			RET_HDIAG(stmt, SQL_STATE_HY000, "bug converting parameter", 0);
	}

	/* apply corrections depending on the (column) size and decimal digits
	 * values given at binding time: nullify or trim the resulted string:
	 * https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/column-size
	 * */
	colsize = get_param_size(irec);
	DBGH(stmt, "requested column size: %llu.", colsize);
	if (colsize) {
		if (colsize < sizeof("yyyy-mm-dd hh:mm") - 1 ||
			colsize == 17 || colsize == 18 ) {
			ERRH(stmt, "invalid column size value: %llu; "
				"allowed: 16, 19, 20+f.", colsize);
			RET_HDIAGS(stmt, SQL_STATE_HY104);
		} else if (colsize == sizeof("yyyy-mm-dd hh:mm") - 1) {
			offt = sizeof("yyyy-mm-ddThh:mm:") - 1;
			offt += /*leading `"`*/1;
			dest[offt ++] = '0';
			dest[offt ++] = '0';
			dest[offt ++] = 'Z';
			*len = offt;
		} else if (colsize == sizeof("yyyy-mm-dd hh:mm:ss") - 1) {
			offt = sizeof("yyyy-mm-ddThh:mm:ss") - 1;
			offt += /*leading `"`*/1;
			dest[offt ++] = 'Z';
			*len = offt;
		} else {
			assert(20 < colsize);
			decdigits = get_param_decdigits(irec);
			DBGH(stmt, "requested decimal digits: %llu.", decdigits);
			if (/*count of fractions in ISO8601 template*/7 < decdigits) {
				INFOH(stmt, "decimal digits value (%hd) reset to 7.");
				decdigits = 7;
			} else if (decdigits == 0) {
				decdigits = -1; /* shave the `.` away */
			}
			if (colsize < decdigits + sizeof("yyyy-mm-ddThh:mm:ss.") - 1) {
				decdigits = colsize - (sizeof("yyyy-mm-ddThh:mm:ss.") - 1);
				WARNH(stmt, "column size adjusted to %hd to fit into a %llu"
					" columns size.", decdigits, colsize);
			}
			offt = sizeof("yyyy-mm-ddThh:mm:ss.") - 1;
			offt += /*leading `"`*/1;
			offt += decdigits;
			dest[offt ++] = 'Z';
			*len = offt;
		}
	} else {
		WARNH(stmt, "column size given 0 -- column size check skipped");
		(*len) ++; /* initial `"` */
	}

	dest[(*len) ++] = '"';
	return SQL_SUCCESS;
}


static SQLRETURN c2sql_cstr2qstr(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	void *data_ptr;
	SQLLEN *octet_len_ptr, cnt;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	/* pointer to read from how many bytes we have */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	cnt = octet_len_ptr ? *octet_len_ptr : strlen((char *)data_ptr);

	if (dest) {
		*dest = '"';
	} else if ((SQLLEN)get_param_size(irec) < cnt) {
		ERRH(stmt, "string's length (%lld) longer than parameter size (%llu).",
			cnt, get_param_size(irec));
		RET_HDIAGS(stmt, SQL_STATE_22001);
	}

	*len = json_escape((char *)data_ptr, cnt, dest + !!dest, SIZE_MAX);

	if (dest) {
		dest[*len + /*1st `"`*/1] = '"';
	}

	*len += /*`"`*/2;

	return SQL_SUCCESS;
}

static SQLRETURN c2sql_wstr2qstr(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	void *data_ptr;
	SQLLEN *octet_len_ptr, cnt, octets;
	int err;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	/* pointer to read from how many bytes we have */
	octet_len_ptr = deferred_address(SQL_DESC_OCTET_LENGTH_PTR, pos, arec);
	/* pointer to app's buffer */
	data_ptr = deferred_address(SQL_DESC_DATA_PTR, pos, arec);

	cnt = octet_len_ptr ? *octet_len_ptr : wcslen((wchar_t *)data_ptr);

	if (dest) {
		*dest = '"';
	} else {
		if ((SQLLEN)get_param_size(irec) < cnt) {
			ERRH(stmt, "string's length (%lld) longer than parameter "
				"size (%llu).", cnt, get_param_size(irec));
			RET_HDIAGS(stmt, SQL_STATE_22001);
		}
	}

	DBGH(stmt, "converting w-string [%lld] `" LWPDL "`; target@0x%p.",
		cnt, cnt, (wchar_t *)data_ptr, dest);
	if (cnt) { /* WCS2U8 will fail with empty string */
		SetLastError(0);
		octets = WCS2U8((wchar_t *)data_ptr, (int)cnt, dest + !!dest,
				dest ? INT_MAX : 0);
		if ((err = GetLastError())) {
			ERRH(stmt, "converting to multibyte string failed: %d", err);
			RET_HDIAGS(stmt, SQL_STATE_HY000);
		}
	} else {
		octets = 0;
	}
	assert(0 <= octets); /* buffer might be empty, so 0 is valid */
	*len = (size_t)octets;

	if (dest) {
		/* last param - buffer len - is calculated as if !dest */
		*len = json_escape_overlapping(dest + /*1st `"`*/1, octets,
				JSON_ESC_SEQ_SZ * octets);
		dest[*len + /*1st `"`*/1] = '"';
	} else {
		/* UCS*-to-UTF8 converted buffer is not yet available, so an accurate
		 * estimation of how long the JSON-escaping would take is not possible
		 * => estimate a worst case: 6x */
		*len *= JSON_ESC_SEQ_SZ;
	}

	*len += /*2x `"`*/2;

	return SQL_SUCCESS;
}

static SQLRETURN c2sql_number2qstr(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	SQLRETURN ret;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	if (dest) {
		*dest = '"';
	}

	ret = c2sql_number(arec, irec, pos, NULL,NULL, 0, dest + !!dest, len);

	if (dest) {
		/* compare lengths only once number has actually been converted */
		if (get_param_size(irec) < *len) {
			ERRH(stmt, "converted number length (%zu) larger than parameter "
				"size (%llu)", *len, get_param_size(irec));
			RET_HDIAGS(stmt, SQL_STATE_22003);
		}
		dest[*len + /*1st `"`*/1] = '"';
	}
	*len += /*2x `"`*/2;

	return ret;
}

SQLRETURN c2sql_varchar(esodbc_rec_st *arec, esodbc_rec_st *irec,
	SQLULEN pos, char *dest, size_t *len)
{
	SQLSMALLINT ctype;
	esodbc_stmt_st *stmt = arec->desc->hdr.stmt;

	switch ((ctype = get_rec_c_type(arec, irec))) {
		case SQL_C_CHAR:
			return c2sql_cstr2qstr(arec, irec, pos, dest, len);
		case SQL_C_WCHAR:
			return c2sql_wstr2qstr(arec, irec, pos, dest, len);

		case SQL_C_BINARY:
			// XXX: json_escape
			ERRH(stmt, "conversion from SQL C BINARY not implemented.");
			RET_HDIAG(stmt, SQL_STATE_HYC00, "conversion from SQL C BINARY "
				"not yet supported", 0);
			break;

		case SQL_C_TINYINT:
		case SQL_C_STINYINT:
		case SQL_C_SHORT:
		case SQL_C_SSHORT:
		case SQL_C_LONG:
		case SQL_C_SLONG:
		case SQL_C_SBIGINT:

		case SQL_C_BIT:
		case SQL_C_UTINYINT:
		case SQL_C_USHORT:
		case SQL_C_ULONG:
		case SQL_C_UBIGINT:

		case SQL_C_FLOAT:
		case SQL_C_DOUBLE:
		case SQL_C_NUMERIC:
			return c2sql_number2qstr(arec, irec, pos, dest, len);

		case SQL_C_TYPE_DATE:
		case SQL_C_TYPE_TIME:
		case SQL_C_TYPE_TIMESTAMP:
			// TODO: leave it a timestamp, or actual DATE/TIME/TS?
			return c2sql_timestamp(arec, irec, pos, dest, len);

		// case SQL_C_GUID:
		default:
			BUGH(stmt, "can't convert SQL C type %hd to timestamp.",
				get_rec_c_type(arec, irec));
			RET_HDIAG(stmt, SQL_STATE_HY000, "bug converting parameter", 0);
	}
}


/* vim: set noet fenc=utf-8 ff=dos sts=0 sw=4 ts=4 : */
