/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * The Original Code is librabbitmq.
 *
 * The Initial Developers of the Original Code are LShift Ltd, Cohesive
 * Financial Technologies LLC, and Rabbit Technologies Ltd.  Portions
 * created before 22-Nov-2008 00:00:00 GMT by LShift Ltd, Cohesive
 * Financial Technologies LLC, or Rabbit Technologies Ltd are Copyright
 * (C) 2007-2008 LShift Ltd, Cohesive Financial Technologies LLC, and
 * Rabbit Technologies Ltd.
 *
 * Portions created by LShift Ltd are Copyright (C) 2007-2009 LShift
 * Ltd. Portions created by Cohesive Financial Technologies LLC are
 * Copyright (C) 2007-2009 Cohesive Financial Technologies
 * LLC. Portions created by Rabbit Technologies Ltd are Copyright (C)
 * 2007-2009 Rabbit Technologies Ltd.
 *
 * Portions created by Tony Garnock-Jones are Copyright (C) 2009-2010
 * LShift Ltd and Tony Garnock-Jones.
 *
 * All Rights Reserved.
 *
 * Contributor(s): ______________________________________.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 2 or later (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of those
 * above. If you wish to allow use of your version of this file only
 * under the terms of the GPL, and not to allow others to use your
 * version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the
 * notice and other provisions required by the GPL. If you do not
 * delete the provisions above, a recipient may use your version of
 * this file under the terms of any one of the MPL or the GPL.
 *
 * ***** END LICENSE BLOCK *****
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "amqp.h"
#include "amqp_private.h"

#include <assert.h>

#define INITIAL_ARRAY_SIZE 16
#define INITIAL_TABLE_SIZE 16

static int amqp_decode_field_value(amqp_bytes_t encoded,
				   amqp_pool_t *pool,
				   amqp_field_value_t *entry,
				   int *offsetptr); /* forward */

static int amqp_encode_field_value(amqp_bytes_t encoded,
				   amqp_field_value_t *entry,
				   int *offsetptr); /* forward */

/*---------------------------------------------------------------------------*/

static int amqp_decode_array(amqp_bytes_t encoded,
			     amqp_pool_t *pool,
			     amqp_array_t *output,
			     int *offsetptr)
{
  int                   limit             = 0;
  int                   offset            = *offsetptr;
  int                   rc                = 0;
  int                   num_entries       = 0;
  amqp_field_value_t   *entries           = NULL;
  int                   allocated_entries = INITIAL_ARRAY_SIZE;

#ifdef __GNUC__
  uint32_t arraysize = D_32(encoded, offset);
#else
  uint32_t arraysize = rc = amqp_d32(encoded, offset);
  
  if( rc < 0 )
	return rc;
#endif

  entries = malloc(INITIAL_ARRAY_SIZE * sizeof(amqp_field_value_t));
  if (entries == NULL) {
    return -ENOMEM;
  }

  offset += 4;
  limit = offset + arraysize;

  while (offset < limit) {
    if (num_entries >= allocated_entries) {
      void *newentries;
      allocated_entries = allocated_entries * 2;
      newentries = realloc(entries, allocated_entries * sizeof(amqp_field_value_t));
      if (newentries == NULL) {
	free(entries);
	return -ENOMEM;
      }
      entries = newentries;
    }

#ifdef __GNUC__ /* frgo, 2010-06-07 */
    AMQP_CHECK_RESULT_CLEANUP(amqp_decode_field_value(encoded,
						      pool,
						      &entries[num_entries],
						      &offset),
			      free(entries));
#else
    {
      int nRC = RABBITMQ_C_NO_ERROR;
      nRC = amqp_decode_field_value(encoded,
                                    pool,
                                    &entries[num_entries],
                                    &offset);
      if( nRC < 0 )
      {
        free( entries );
        return nRC;
      }
    }
#endif

	num_entries++;
  }

  output->num_entries = num_entries;
  output->entries = amqp_pool_alloc(pool, num_entries * sizeof(amqp_field_value_t));
  if (output->entries == NULL && num_entries > 0) {
    /* NULL is legitimate if we requested a zero-length block. */
    free(entries);
    return -ENOMEM;
  }

  memcpy(output->entries, entries, num_entries * sizeof(amqp_field_value_t));
  free(entries);

  *offsetptr = offset;
  return 0;
}

int amqp_decode_table(amqp_bytes_t encoded,
		      amqp_pool_t *pool,
		      amqp_table_t *output,
		      int *offsetptr)
{
  int                 limit             = 0;
  int                 offset            = *offsetptr;
  int                 rc                = 0;
  int                 num_entries       = 0;
  amqp_table_entry_t *entries           = NULL;
  int                 allocated_entries = INITIAL_TABLE_SIZE;

  uint32_t tablesize =
#ifdef __GNUC__
    D_32(encoded, offset);
#else
    rc = amqp_d32( encoded, offset);
    if( rc < 0 )
	  return rc;
#endif

  entries =  (amqp_table_entry_t *) malloc(INITIAL_TABLE_SIZE * sizeof(amqp_table_entry_t));

  if (entries == NULL) {
    return -ENOMEM;
  }

  offset += 4;
  limit = offset + tablesize;

  while (offset < limit) {
    size_t keylen;
    amqp_table_entry_t *entry;

	keylen =
#ifdef __GNUC__
    D_8(encoded, offset);
#else
	rc = amqp_d8( encoded, offset );
    if( rc < 0 )
	  return rc;
#endif

    offset++;

    if (num_entries >= allocated_entries) {
      void *newentries;
      allocated_entries = allocated_entries * 2;
      newentries = realloc(entries, allocated_entries * sizeof(amqp_table_entry_t));
      if (newentries == NULL) {
	free(entries);
	return -ENOMEM;
      }
      entries = (amqp_table_entry_t *) newentries;
    }
    entry = &entries[num_entries];

    entry->key.len = keylen;
    entry->key.bytes =
#ifdef __GNUC__
		D_BYTES(encoded, offset, keylen);
#else
		amqp_dbytes( encoded, offset, keylen );
#endif
    offset += keylen;

#ifdef __GNUC__ /* frgo, 2010-06-07 */
    AMQP_CHECK_RESULT_CLEANUP(amqp_decode_field_value(encoded,
                                                      pool,
                                                      &entry->value,
                                                      &offset),
                              free(entries));
#else
    {
      int nRC = RABBITMQ_C_NO_ERROR;

	  nRC = amqp_decode_field_value(encoded,
                                    pool,
                                    &entry->value,
                                    &offset);
      if( nRC < 0 )
      {
        free( entries );
        return nRC;
      }
    }
#endif

	num_entries++;
  }

  output->num_entries = num_entries;
  output->entries = (amqp_table_entry_t *) amqp_pool_alloc(pool, num_entries * sizeof(amqp_table_entry_t));
  if (output->entries == NULL && num_entries > 0) {
    /* NULL is legitimate if we requested a zero-length block. */
    free(entries);
    return -ENOMEM;
  }

  memcpy(output->entries, entries, num_entries * sizeof(amqp_table_entry_t));
  free(entries);

  *offsetptr = offset;
  return 0;
}

static int amqp_decode_field_value(amqp_bytes_t encoded,
				   amqp_pool_t *pool,
				   amqp_field_value_t *entry,
				   int *offsetptr)
{
  int      offset = *offsetptr;
  int64_t  rc     = 0;  

  entry->kind =
#ifdef __GNUC__
    D_8(encoded, offset);
#else
    amqp_d8( encoded, offset );
    if((rc = entry->kind) < 0 )
	  return (int) rc;
#endif

  offset++;

  switch (entry->kind) 
  {
    case AMQP_FIELD_KIND_BOOLEAN:
      entry->value.boolean = 
#ifdef __GNUC__
		  D_8(encoded, offset) ? 1 : 0;
#else
		  amqp_d8( encoded, offset ) ? 1 : 0;

	  if(( rc = entry->value.boolean ) < 0 )
	    return (int) rc;
#endif

      offset++;
      break;

	case AMQP_FIELD_KIND_I8:
      entry->value.i8 = 
#ifdef __GNUC__
		  (int8_t) D_8(encoded, offset);
#else
		  (int8_t) amqp_d8( encoded, offset );
	  if(( rc =  entry->value.i8 ) < 0 )
		return (int) rc;
#endif
      offset++;
      break;

	case AMQP_FIELD_KIND_U8:
      entry->value.u8 =
#ifdef __GNUC__
		  D_8(encoded, offset);
#else
		  amqp_d8( encoded, offset );
 	  if(( rc = entry->value.u8 ) < 0 )
		return (int) rc;
#endif
      offset++;
      break;

	case AMQP_FIELD_KIND_I16:
      entry->value.i16 = (int16_t)
#ifdef __GNUC__
		  D_16(encoded, offset);
#else
		  amqp_d16( encoded, offset );
  	  if(( rc = entry->value.i16 ) < 0 )
		return (int) rc;
#endif
      offset += 2;
      break;

	case AMQP_FIELD_KIND_U16:
      entry->value.u16 = 
#ifdef __GNUC__
		  D_16(encoded, offset);
#else
		 amqp_d16( encoded, offset );
  	  if(( rc = entry->value.u16 ) < 0 )
		return (int) rc;
#endif
      offset += 2;
      break;

	case AMQP_FIELD_KIND_I32:
      entry->value.i32 = 
#ifdef __GNUC__
		  (int32_t) D_32(encoded, offset);
#else
		  (int32_t) amqp_d32( encoded, offset );
		if(( rc = entry->value.i32 ) < 0 )
		  return (int) rc;
#endif
      offset += 4;
      break;

    case AMQP_FIELD_KIND_U32:
      entry->value.u32 = 
#ifdef __GNUC__
		  D_32(encoded, offset);
#else
		  amqp_d32( encoded, offset );
		if(( rc = entry->value.u32 )  < 0 )
		  return (int) rc;
#endif
      offset += 4;
      break;

    case AMQP_FIELD_KIND_I64:
      entry->value.i64 =
#ifdef __GNUC__
		  (int64_t) D_64(encoded, offset);
#else
		  (int64_t) amqp_d64( encoded, offset );
	  entry->value.i64;
	  if(( rc = entry->value.i64 ) < 0 )
		return (int) rc;
#endif
      offset += 8;
      break;

    case AMQP_FIELD_KIND_F32:
      entry->value.u32 = 
#ifdef __GNUC__
		  D_32(encoded, offset);
#else
		  amqp_d32( encoded, offset );
	  if(( rc = entry->value.u32 ) < 0 )
		return (int) rc;
#endif
      /* and by punning, f32 magically gets the right value...! */
      offset += 4;
      break;

    case AMQP_FIELD_KIND_F64:
      entry->value.u64 =
#ifdef __GNUC__
		  D_64(encoded, offset);
#else
		  amqp_d64( encoded, offset );
	  if(( rc = entry->value.u64 ) < 0 )
		return (int) rc;
#endif
      /* and by punning, f64 magically gets the right value...! */
      offset += 8;
      break;

    case AMQP_FIELD_KIND_DECIMAL:
      entry->value.decimal.decimals =
#ifdef __GNUC__
		 D_8(encoded, offset);
#else
		 amqp_d8(encoded, offset);
	  if(( rc = entry->value.decimal.decimals ) < 0 )
		return (int) rc;
#endif
      offset++;
      entry->value.decimal.value = 
#ifdef __GNUC__
		  D_32(encoded, offset);
#else
		  amqp_d32( encoded, offset );
	  if(( rc = entry->value.decimal.value ) < 0 )
		return (int) rc;
#endif
      offset += 4;
      break;

    case AMQP_FIELD_KIND_UTF8:
      /* AMQP_FIELD_KIND_UTF8 and AMQP_FIELD_KIND_BYTES have the
	 same implementation, but different interpretations. */
      /* fall through */

    case AMQP_FIELD_KIND_BYTES:
      entry->value.bytes.len = 
#ifdef __GNUC__
		  D_32(encoded, offset);
#else
		  amqp_d32( encoded, offset );
	  if(( rc = entry->value.bytes.len ) < 0 )
		return (int) rc;
#endif
      offset += 4;
      entry->value.bytes.bytes =
#ifdef __GNUC__
		  D_BYTES(encoded, offset, entry->value.bytes.len);
#else
		  amqp_dbytes( encoded, offset, entry->value.bytes.len );
#endif
      offset += entry->value.bytes.len;
      break;

    case AMQP_FIELD_KIND_ARRAY:

#ifdef __GNUC__ /* frgo, 2010-06-07 */
      AMQP_CHECK_RESULT(amqp_decode_array(encoded, pool, &(entry->value.array), &offset));
#else
      {
        int nRC = RABBITMQ_C_NO_ERROR;

        nRC = amqp_decode_array(encoded, pool, &(entry->value.array), &offset);
        if( nRC < 0 )
          return nRC;
      }
#endif
      break;

    case AMQP_FIELD_KIND_TIMESTAMP:
      entry->value.u64 = 
#ifdef __GNUC__
		  D_64(encoded, offset);
#else
		  amqp_d64( encoded, offset );
	  if(( rc = entry->value.u64 ) < 0 )
		return (int) rc;
#endif
      offset += 8;
      break;

    case AMQP_FIELD_KIND_TABLE:

#ifdef __GNUC__ /* frgo, 2010-06-07 */
      AMQP_CHECK_RESULT(amqp_decode_table(encoded, pool, &(entry->value.table), &offset));
#else
      {
        int nRC = RABBITMQ_C_NO_ERROR;

        nRC = amqp_decode_table(encoded, pool, &(entry->value.table), &offset);
        if( nRC < 0 )
          return nRC;
      }
#endif
      break;

    case AMQP_FIELD_KIND_VOID:
      break;
    
	default:
      return -EINVAL;
  }

  *offsetptr = offset;
  
  return 0;
}

/*---------------------------------------------------------------------------*/

static int amqp_encode_array(amqp_bytes_t encoded,
			     amqp_array_t *input,
			     int *offsetptr)
{
  int offset = *offsetptr;
  int arraysize_offset = offset;
  int i;

  offset += 4; /* skip space for the size of the array to be filled in later */

  for (i = 0; i < input->num_entries; i++) {

#ifdef __GNUC__ /* frgo, 2010-06-07 */
    AMQP_CHECK_RESULT(amqp_encode_field_value(encoded, &(input->entries[i]), &offset));
#else
    {
      int nRC = RABBITMQ_C_NO_ERROR;

	  nRC = amqp_encode_field_value(encoded, &(input->entries[i]), &offset);
      if( nRC < 0 )
        return nRC;
    }
#endif
  }

  E_32(encoded, arraysize_offset, (offset - *offsetptr - 4));
  *offsetptr = offset;
  return 0;
}

int amqp_encode_table(amqp_bytes_t encoded,
		      amqp_table_t *input,
		      int *offsetptr)
{
  int offset = *offsetptr;
  int tablesize_offset = offset;
  int i;

  offset += 4; /* skip space for the size of the table to be filled in later */

  for (i = 0; i < input->num_entries; i++) {
    amqp_table_entry_t *entry = &(input->entries[i]);

    E_8(encoded, offset, entry->key.len);
    offset++;

    E_BYTES(encoded, offset, entry->key.len, entry->key.bytes);
    offset += entry->key.len;

#ifdef __GNUC__ /* frgo, 2010-06-07 */
    AMQP_CHECK_RESULT(amqp_encode_field_value(encoded, &(entry->value), &offset));
#else
    {
      int nRC = RABBITMQ_C_NO_ERROR;

      nRC = amqp_encode_field_value(encoded, &(entry->value), &offset);
      if( nRC < 0 )
        return nRC;
    }
#endif
  }

  E_32(encoded, tablesize_offset, (offset - *offsetptr - 4));
  *offsetptr = offset;
  return 0;
}

static int amqp_encode_field_value(amqp_bytes_t encoded,
				   amqp_field_value_t *entry,
				   int *offsetptr)
{
  int offset = *offsetptr;

  E_8(encoded, offset, entry->kind);
  offset++;

  switch (entry->kind) {
    case AMQP_FIELD_KIND_BOOLEAN:
      E_8(encoded, offset, entry->value.boolean ? 1 : 0);
      offset++;
      break;
    case AMQP_FIELD_KIND_I8:
      E_8(encoded, offset, (uint8_t) entry->value.i8);
      offset++;
      break;
    case AMQP_FIELD_KIND_U8:
      E_8(encoded, offset, entry->value.u8);
      offset++;
      break;
    case AMQP_FIELD_KIND_I16:
      E_16(encoded, offset, (uint16_t) entry->value.i16);
      offset += 2;
      break;
    case AMQP_FIELD_KIND_U16:
      E_16(encoded, offset, entry->value.u16);
      offset += 2;
      break;
    case AMQP_FIELD_KIND_I32:
      E_32(encoded, offset, (uint32_t) entry->value.i32);
      offset += 4;
      break;
    case AMQP_FIELD_KIND_U32:
      E_32(encoded, offset, entry->value.u32);
      offset += 4;
      break;
    case AMQP_FIELD_KIND_I64:
      E_64(encoded, offset, (uint64_t) entry->value.i64);
      offset += 8;
      break;
    case AMQP_FIELD_KIND_F32:
      /* by punning, u32 magically gets the right value...! */
      E_32(encoded, offset, entry->value.u32);
      offset += 4;
      break;
    case AMQP_FIELD_KIND_F64:
      /* by punning, u64 magically gets the right value...! */
      E_64(encoded, offset, entry->value.u64);
      offset += 8;
      break;
    case AMQP_FIELD_KIND_DECIMAL:
      E_8(encoded, offset, entry->value.decimal.decimals);
      offset++;
      E_32(encoded, offset, entry->value.decimal.value);
      offset += 4;
      break;
    case AMQP_FIELD_KIND_UTF8:
      /* AMQP_FIELD_KIND_UTF8 and AMQP_FIELD_KIND_BYTES have the
	 same implementation, but different interpretations. */
      /* fall through */
    case AMQP_FIELD_KIND_BYTES:
      E_32(encoded, offset, entry->value.bytes.len);
      offset += 4;
      E_BYTES(encoded, offset, entry->value.bytes.len, entry->value.bytes.bytes);
      offset += entry->value.bytes.len;
      break;
    case AMQP_FIELD_KIND_ARRAY:
#ifdef __GNUC__ /* frgo, 2010-06-11 */
      AMQP_CHECK_RESULT(amqp_encode_array(encoded, &(entry->value.array), &offset));
#else
      {
        int nRC = RABBITMQ_C_NO_ERROR;

        nRC = amqp_encode_array(encoded, &(entry->value.array), &offset);
        if( nRC < 0 )
          return nRC;
      }
#endif
      break;
    case AMQP_FIELD_KIND_TIMESTAMP:
      E_64(encoded, offset, entry->value.u64);
      offset += 8;
      break;
    case AMQP_FIELD_KIND_TABLE:
#ifdef __GNUC__ /*frgo, 2010-06-11 */
      AMQP_CHECK_RESULT(amqp_encode_table(encoded, &(entry->value.table), &offset));
#else
      {
        int nRC = RABBITMQ_C_NO_ERROR;

        nRC = amqp_encode_table(encoded, &(entry->value.table), &offset);
        if( nRC < 0 )
          return nRC;
      }
#endif
      break;
    case AMQP_FIELD_KIND_VOID:
      break;
    default:
      return -EINVAL;
  }

  *offsetptr = offset;
  return 0;
}

/*---------------------------------------------------------------------------*/

int amqp_table_entry_cmp(void const *entry1, void const *entry2) {
  amqp_table_entry_t const *p1 = (amqp_table_entry_t const *) entry1;
  amqp_table_entry_t const *p2 = (amqp_table_entry_t const *) entry2;

  int d;
  int minlen;

  minlen = p1->key.len;
  if (p2->key.len < minlen) minlen = p2->key.len;

  d = memcmp(p1->key.bytes, p2->key.bytes, minlen);
  if (d != 0) {
    return d;
  }

  return p1->key.len - p2->key.len;
}
